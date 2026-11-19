// core/caches.cpp -- see caches.h. Moved verbatim from mctde_launcher.cpp; only the file I/O now
// goes through core/platform.h. Signatures hash (size, FILETIME) exactly as before, and the Linux
// platform layer reports FILETIME the way Wine does, so a cache baked on either side stays valid.
#include "caches.h"
#include "platform.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

static bool IEq(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return *a == *b;
}

// ============================================================ DSFont bitmap cache (Steam-name coverage)
// Bakes the game's OWN DSFont24 UI font (DATA\font\<lang>\DSFont24.tpf + .ccm) into a merged glyph
// blob that mctde_overlay.dll loads into its ImGui atlas. English is the base UI font; Japanese/
// Korean/TChinese glyphs are merged in (deduped, English wins ties) so arbitrary Steam names render
// instead of turning into boxes. Generated from the user's own files on first launch -- nothing
// copyrighted ships. Output: <ChainloadDir>\mctde_dsfont.bin (cached; regenerated only when the
// source font files change). All binary parsing here (TPF DDS scan, BC3 alpha decode, CCM v1) was
// validated byte-exact against texconv/SoulsFormats before porting.
namespace dsfont {

static const uint32_t BLOB_VERSION = 1;

static uint32_t rdU32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int16_t  rdS16(const uint8_t* p){ return (int16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8)); }
static float    rdF32(const uint8_t* p){ uint32_t u=rdU32(p); float f; memcpy(&f,&u,4); return f; }

static std::vector<uint8_t> ReadAll(const std::string& path){ return plat::ReadAll(path); }

struct Glyph { uint32_t code; int x,y,w,h,pre,adv,tex; };

// Decode a WxH DXT5 page's ALPHA channel (RGB is white) into out[W*H], row-major. Verified
// byte-exact vs texconv across all 262144 px of page 0.
static void DecodeBC3Alpha(const uint8_t* dds, int W, int H, std::vector<uint8_t>& out){
    out.assign((size_t)W*H,0);
    const uint8_t* blk=dds+0x80; int bpr=W/4;   // 128-byte DDS header
    for(int by=0;by<H/4;++by) for(int bx=0;bx<bpr;++bx){
        const uint8_t* b=blk+((size_t)(by*bpr+bx)*16);
        int a0=b[0],a1=b[1],pal[8]; pal[0]=a0; pal[1]=a1;
        if(a0>a1){ for(int i=1;i<=6;++i) pal[i+1]=((7-i)*a0+i*a1+3)/7; }
        else{ for(int i=1;i<=4;++i) pal[i+1]=((5-i)*a0+i*a1+2)/5; pal[6]=0; pal[7]=255; }
        uint32_t lo=(uint32_t)b[2]|((uint32_t)b[3]<<8)|((uint32_t)b[4]<<16);
        uint32_t hi=(uint32_t)b[5]|((uint32_t)b[6]<<8)|((uint32_t)b[7]<<16);
        for(int t=0;t<16;++t){ int idx = t<8 ? (int)((lo>>(3*t))&7) : (int)((hi>>(3*(t-8)))&7);
            int px=bx*4+(t&3), py=by*4+(t>>2); out[(size_t)py*W+px]=(uint8_t)pal[idx]; }
    }
}

// A DS1 .tpf embeds one full DDS per page; scan for the "DDS " magic and decode each (page order
// == CCM TexIndex order).
static bool LoadTpfPages(const std::string& path, std::vector<std::vector<uint8_t>>& pages, int& W, int& H){
    std::vector<uint8_t> d=ReadAll(path); if(d.size()<0x80) return false; W=H=0;
    for(size_t i=0;i+0x80<=d.size();){
        if(!(d[i]=='D'&&d[i+1]=='D'&&d[i+2]=='S'&&d[i+3]==' ')){ ++i; continue; }
        if(i+0x58>d.size()) break;
        int h=(int)rdU32(&d[i+0x0C]), w=(int)rdU32(&d[i+0x10]);
        if(w<=0||h<=0||w>4096||h>4096||(w&3)||(h&3)){ ++i; continue; }
        if(!(d[i+0x54]=='D'&&d[i+0x55]=='X'&&d[i+0x56]=='T'&&d[i+0x57]=='5')){ ++i; continue; }
        size_t topBytes=(size_t)(w/4)*(h/4)*16;
        if(i+0x80+topBytes>d.size()){ ++i; continue; }
        if(W==0){ W=w; H=h; }
        std::vector<uint8_t> a; DecodeBC3Alpha(&d[i],w,h,a); pages.push_back(std::move(a));
        i += 0x80+topBytes;   // skip this page (incl. its mip chain remainder is re-scanned harmlessly)
    }
    return !pages.empty();
}

// SoulsFormats CCM v1 (DS1): normalized-UV glyph rects resolved through code groups.
static bool LoadCcm(const std::string& path, std::vector<Glyph>& out){
    std::vector<uint8_t> d=ReadAll(path); if(d.size()<0x20) return false;
    uint32_t fsize=rdU32(&d[4]);
    int texW=rdS16(&d[0x0A]), texH=rdS16(&d[0x0C]);
    int cg=rdS16(&d[0x10]); uint32_t goff=rdU32(&d[0x18]);
    if(goff<0x20 || goff>d.size() || texW<=0 || texH<=0) return false;
    int gcount=(int)((fsize-goff)/24);
    if(gcount<=0 || goff+(size_t)gcount*24>d.size()) return false;
    std::vector<Glyph> gl((size_t)gcount);
    for(int i=0;i<gcount;++i){ const uint8_t* p=&d[goff+(size_t)24*i];
        float u1x=rdF32(p),u1y=rdF32(p+4),u2x=rdF32(p+8),u2y=rdF32(p+12);
        Glyph g; g.code=0;
        g.x=(int)(u1x*texW+0.5f); g.y=(int)(u1y*texH+0.5f);
        g.w=(int)((u2x-u1x)*texW+0.5f); g.h=(int)((u2y-u1y)*texH+0.5f);
        g.pre=rdS16(p+16); g.adv=rdS16(p+20); g.tex=rdS16(p+22);
        gl[(size_t)i]=g;
    }
    for(int i=0;i<cg;++i){ const uint8_t* p=&d[0x20+(size_t)12*i];
        int s=(int)rdU32(p), e=(int)rdU32(p+4), gi=(int)rdU32(p+8);
        if(e<s || e-s>0x20000) continue;
        for(int c=s;c<=e;++c){ int idx=gi+(c-s); if(idx>=0&&idx<gcount){ Glyph g=gl[(size_t)idx]; g.code=(uint32_t)c; out.push_back(g);} }
    }
    return true;
}

// FNV-1a over (size, mtime) of each present source file -> staleness key.
static uint32_t SourceSignature(const std::string& fontDir, const char* const* langs, int nlang){
    uint32_t hsh=2166136261u;
    auto mix=[&](const void* pp,size_t n){ const uint8_t* p=(const uint8_t*)pp; for(size_t i=0;i<n;++i){ hsh^=p[i]; hsh*=16777619u; } };
    for(int i=0;i<nlang;++i) for(int k=0;k<2;++k){
        std::string f=fontDir+langs[i]+(k?"\\DSFont24.ccm":"\\DSFont24.tpf");
        uint32_t lo,hi; uint64_t ft;
        if(plat::StatForSignature(f,lo,hi,ft)){
            mix(&lo,4); mix(&hi,4); mix(&ft,8);
        }
    }
    hsh ^= BLOB_VERSION*2654435761u;
    return hsh;
}

// Extract + merge all glyphs, write the blob. Returns true on success.
static bool Generate(const std::string& fontDir, const std::string& blobPath){
    const char* langs[]={"english","japanese","korean","tchinese"};
    const int NL=4;

    struct Out { uint32_t code; int w,h,xoff,adv; std::vector<uint8_t> bmp; };
    std::vector<Out> out; out.reserve(12000);
    std::unordered_set<uint32_t> seen; seen.reserve(24000);
    int maxH=0; bool anyEnglish=false;

    for(int li=0; li<NL; ++li){
        std::string lb=fontDir+langs[li];
        std::vector<Glyph> glyphs;
        if(!LoadCcm(lb+"\\DSFont24.ccm", glyphs)) continue;
        std::vector<std::vector<uint8_t>> pages; int W=0,H=0;
        if(!LoadTpfPages(lb+"\\DSFont24.tpf", pages, W, H)) continue;
        if(li==0) anyEnglish=true;
        for(const Glyph& g : glyphs){
            if(g.code==0 || seen.count(g.code)) continue;
            if(g.tex<0 || g.tex>=(int)pages.size()) continue;
            if(g.w<=0||g.h<=0||g.x<0||g.y<0||g.x+g.w>W||g.y+g.h>H) continue;
            Out o; o.code=g.code; o.w=g.w; o.h=g.h; o.xoff=g.pre; o.adv=(g.adv>0?g.adv:g.pre+g.w);
            o.bmp.resize((size_t)g.w*g.h);
            const std::vector<uint8_t>& pg=pages[(size_t)g.tex];
            for(int r=0;r<g.h;++r) memcpy(&o.bmp[(size_t)r*g.w], &pg[(size_t)(g.y+r)*W+g.x], (size_t)g.w);
            if(g.h>maxH) maxH=g.h;
            seen.insert(g.code); out.push_back(std::move(o));
        }
    }
    if(!anyEnglish || out.empty()) return false;

    int lineHeight=maxH+3, baseY=(maxH>=5?maxH-5:maxH);

    std::vector<uint8_t> blob; blob.reserve(9u*1024*1024);
    auto pushN =[&](const void* p,size_t n){ const uint8_t* b=(const uint8_t*)p; blob.insert(blob.end(),b,b+n); };
    auto pushU32=[&](uint32_t v){ pushN(&v,4); };
    auto pushU16=[&](uint16_t v){ pushN(&v,2); };
    auto pushS16=[&](int16_t v){ pushN(&v,2); };
    pushN("MCTDEFNT",8);
    pushU32(BLOB_VERSION);
    pushU32(SourceSignature(fontDir,langs,NL));
    pushU16((uint16_t)lineHeight); pushU16((uint16_t)baseY); pushU16((uint16_t)maxH); pushU16(0);
    pushU32((uint32_t)out.size());
    for(const Out& o : out){
        pushU32(o.code);
        pushU16((uint16_t)o.w); pushU16((uint16_t)o.h);
        pushS16((int16_t)o.xoff); pushS16((int16_t)(maxH-o.h)); pushS16((int16_t)o.adv); pushU16(0);
        pushN(o.bmp.data(), o.bmp.size());
    }

    if(!plat::WriteAtomic(blobPath, blob.data(), blob.size())) return false;

    // sidecar log so a first-run generation is auditable
    char buf[256]; int n=snprintf(buf,sizeof(buf),"glyphs=%u maxH=%d lineHeight=%d base=%d bytes=%u\r\n",
        (unsigned)out.size(),maxH,lineHeight,baseY,(unsigned)blob.size());
    if(n>0) plat::WriteText(blobPath+".log", std::string(buf,(size_t)n));
    return true;
}

// (Re)generate the cache if missing or the source fonts changed. Cheap no-op when current.
static void GenerateFontCacheIfNeeded(const std::string& dataDir, const std::string& blobPath){
    std::string fontDir=dataDir+"font\\";
    const char* langs[]={"english","japanese","korean","tchinese"};
    uint32_t sig=SourceSignature(fontDir,langs,4);
    std::vector<uint8_t> cur=ReadAll(blobPath);
    if(cur.size()>=16 && memcmp(cur.data(),"MCTDEFNT",8)==0
       && rdU32(&cur[8])==BLOB_VERSION && rdU32(&cur[12])==sig)
        return;  // up to date
    Generate(fontDir, blobPath);
}

} // namespace dsfont

// ============================================================ DS covenant icon cache (classic mode)
// Bakes 8 covenant/role icons out of the game's own icon atlases (menu\menu_0-tpf\Icon20.dds and
// Icon40.dds) into mctde_dsicons.bin, which mctde_overlay draws next to phantom names in Classic
// Font mode. Icon positions come straight from the params (EquipParamGoods/Accessory iconId), so
// they are exact. Generated from the user's own files -- nothing copyrighted ships.
namespace dsicons {

static const uint32_t ICO_VERSION = 5;
static const int ICO_SZ = 28;   // baked square size; overlay scales to line height
static const int ICO_COUNT = 11;

// type index -> (atlas 0=Icon20 / 1=Icon40 / 2=Icon30 / 3=Menu04_2, source cell rect on that atlas)
struct IcoDef { int atlas, x0, y0, x1, y1; };
static const IcoDef ICO[ICO_COUNT] = {
    { 0,   0,   0,  81,  92 },  // 0 White Summon   : White Sign Soapstone (Icon20 cell0)
    { 0,  81,   0, 163,  92 },  // 1 Red Summon     : Red Sign Soapstone   (cell1)
    { 0, 163,   0, 246,  92 },  // 2 Red Invader    : Red Eye Orb          (cell2)
    { 0, 819,   0, 901,  92 },  // 3 Darkmoon       : Blue Eye Orb         (cell10)
    { 0, 246, 644, 328, 735 },  // 4 Gravelord      : Eye of Death         (cell87)
    { 1, 409, 277, 491, 368 },  // 5 Forest Hunter  : Cat Covenant Ring    (Icon40 cell41)
    { 0, 901, 184,1003, 277 },  // 6 Sun Summon     : Sunlight Medal       (cell35)
    { 0, 901,   0,1003,  92 },  // 7 Dragon phantom : Dragon Eye           (cell11)
    { 0, 410, 828, 492, 920 },  // 8 Host (human)   : Twin Humanities / Split Humanity (Icon20 col5,row9)
    { 0, 328, 828, 410, 920 },  // 9 Host (hollow)  : Humanity                         (Icon20 col4,row9)
    { 3, 126, 430, 168, 472 },  //10 Local marker   : gold lightning framed tile        (Menu04_2 col1,rowB)
};

// Decode a DXT5 (BC3) DDS to full RGBA.
static void DecodeBC3RGBA(const uint8_t* dds, int W, int H, std::vector<uint8_t>& out){
    out.assign((size_t)W*H*4, 0);
    const uint8_t* blk = dds + 0x80; int bpr = W/4;
    for(int by=0;by<H/4;++by) for(int bx=0;bx<bpr;++bx){
        const uint8_t* b = blk + ((size_t)(by*bpr+bx)*16);
        int a0=b[0],a1=b[1],ap[8]; ap[0]=a0; ap[1]=a1;
        if(a0>a1){ for(int i=1;i<=6;++i) ap[i+1]=((7-i)*a0+i*a1+3)/7; }
        else{ for(int i=1;i<=4;++i) ap[i+1]=((5-i)*a0+i*a1+2)/5; ap[6]=0; ap[7]=255; }
        uint32_t alo=(uint32_t)b[2]|((uint32_t)b[3]<<8)|((uint32_t)b[4]<<16);
        uint32_t ahi=(uint32_t)b[5]|((uint32_t)b[6]<<8)|((uint32_t)b[7]<<16);
        uint16_t c0=(uint16_t)(b[8]|(b[9]<<8)), c1=(uint16_t)(b[10]|(b[11]<<8));
        int r[4],g[4],bl[4];
        r[0]=((c0>>11)&0x1f)*255/31; g[0]=((c0>>5)&0x3f)*255/63; bl[0]=(c0&0x1f)*255/31;
        r[1]=((c1>>11)&0x1f)*255/31; g[1]=((c1>>5)&0x3f)*255/63; bl[1]=(c1&0x1f)*255/31;
        r[2]=(2*r[0]+r[1])/3; g[2]=(2*g[0]+g[1])/3; bl[2]=(2*bl[0]+bl[1])/3;
        r[3]=(r[0]+2*r[1])/3; g[3]=(g[0]+2*g[1])/3; bl[3]=(bl[0]+2*bl[1])/3;
        uint32_t ci=(uint32_t)b[12]|((uint32_t)b[13]<<8)|((uint32_t)b[14]<<16)|((uint32_t)b[15]<<24);
        for(int t=0;t<16;++t){
            int ai = t<8 ? (int)((alo>>(3*t))&7) : (int)((ahi>>(3*(t-8)))&7);
            int cx = (int)((ci>>(2*t))&3);
            int px=bx*4+(t&3), py=by*4+(t>>2); size_t o=((size_t)py*W+px)*4;
            out[o]=(uint8_t)r[cx]; out[o+1]=(uint8_t)g[cx]; out[o+2]=(uint8_t)bl[cx]; out[o+3]=(uint8_t)ap[ai];
        }
    }
}

// mask -> right-shift (trailing zero bits)
static int MaskShift(uint32_t m){ if(!m) return 0; int s=0; while(!(m&1)){ m>>=1; ++s; } return s; }

// Extract the raw DDS bytes of a named texture out of a DS1 PTDE .tpf. Entries are 0x14 bytes
// (dataOffset, dataSize, format/type/mip/flags, nameOffset, unk); names are null-terminated ASCII at
// nameOffset. Matches on the basename (case-insensitive) so "Icon20" finds it regardless of any path.
// Validates the DDS magic before returning. This is how normal (UDSFM-only, not Yabber-unpacked)
// installs are read -- the loose DDS path is just a fast pre-check for further-unpacked installs.
static bool TpfExtractDDS(const std::string& tpfPath, const char* texName, std::vector<uint8_t>& out){
    std::vector<uint8_t> d = dsfont::ReadAll(tpfPath);
    if(d.size()<0x10 || !(d[0]=='T'&&d[1]=='P'&&d[2]=='F'&&d[3]==0)) return false;   // magic is "TPF\0"
    uint32_t count = dsfont::rdU32(&d[8]);
    if(count==0 || count>4096) return false;
    const size_t ENT=0x14;
    if(0x10+(size_t)count*ENT > d.size()) return false;
    for(uint32_t i=0;i<count;++i){
        const uint8_t* e=&d[0x10+(size_t)i*ENT];
        uint32_t off=dsfont::rdU32(e), sz=dsfont::rdU32(e+4), noff=dsfont::rdU32(e+0x0C);
        if(noff>=d.size()) continue;
        size_t maxn=d.size()-noff, nl=0; while(nl<maxn && d[noff+nl]) ++nl;
        std::string name((const char*)&d[noff], nl);
        size_t slash=name.find_last_of("\\/"); const char* base = (slash==std::string::npos)? name.c_str() : name.c_str()+slash+1;
        if(!IEq(base, texName)) continue;
        if((size_t)off+sz > d.size() || sz<0x80) return false;
        if(!(d[off]=='D'&&d[off+1]=='D'&&d[off+2]=='S'&&d[off+3]==' ')) return false;
        out.assign(d.begin()+off, d.begin()+off+sz);
        return true;
    }
    return false;
}

// Parse a DDS (DXT5 or uncompressed 32-bit) already in memory to RGBA. Returns false if unsupported.
static bool LoadDDSBytes(const std::vector<uint8_t>& d, std::vector<uint8_t>& rgba, int& W, int& H){
    if(d.size()<0x80 || !(d[0]=='D'&&d[1]=='D'&&d[2]=='S'&&d[3]==' ')) return false;
    H = (int)dsfont::rdU32(&d[0x0C]); W = (int)dsfont::rdU32(&d[0x10]);
    if(W<=0||H<=0||W>8192||H>8192) return false;

    if(d[0x54]=='D'&&d[0x55]=='X'&&d[0x56]=='T'&&d[0x57]=='5'){        // BC3 / DXT5
        if((W&3)||(H&3)) return false;
        if((size_t)0x80 + (size_t)(W/4)*(H/4)*16 > d.size()) return false;
        DecodeBC3RGBA(d.data(), W, H, rgba);
        return true;
    }
    // Uncompressed 32bpp (some atlases, e.g. Icon40, ship as raw A8R8G8B8, odd widths allowed).
    uint32_t bits = dsfont::rdU32(&d[0x58]);
    if(bits == 32){
        if((size_t)0x80 + (size_t)W*H*4 > d.size()) return false;
        uint32_t rm=dsfont::rdU32(&d[0x5C]), gm=dsfont::rdU32(&d[0x60]), bm=dsfont::rdU32(&d[0x64]), am=dsfont::rdU32(&d[0x68]);
        if(!rm&&!gm&&!bm){ rm=0x00FF0000; gm=0x0000FF00; bm=0x000000FF; am=0xFF000000; }  // assume A8R8G8B8
        int rs=MaskShift(rm), gs=MaskShift(gm), bs=MaskShift(bm), as=MaskShift(am);
        rgba.assign((size_t)W*H*4, 0);
        const uint8_t* px = &d[0x80];
        for(size_t i=0; i<(size_t)W*H; ++i){
            uint32_t v = dsfont::rdU32(px + i*4);
            rgba[i*4+0]=(uint8_t)((v&rm)>>rs);
            rgba[i*4+1]=(uint8_t)((v&gm)>>gs);
            rgba[i*4+2]=(uint8_t)((v&bm)>>bs);
            rgba[i*4+3]= am ? (uint8_t)((v&am)>>as) : 255;
        }
        return true;
    }
    return false;
}

static bool LoadDDS(const std::string& path, std::vector<uint8_t>& rgba, int& W, int& H){
    std::vector<uint8_t> d = dsfont::ReadAll(path);
    return LoadDDSBytes(d, rgba, W, H);
}

// Load a source atlas by texture name: prefer the loose DDS (Yabber-unpacked installs), otherwise
// pull it straight out of the packed .tpf (the normal UDSFM-only layout most users have).
static bool LoadAtlas(const std::string& looseDdsPath, const std::string& tpfPath, const char* texName,
                      std::vector<uint8_t>& rgba, int& W, int& H){
    if(LoadDDS(looseDdsPath, rgba, W, H)) return true;
    std::vector<uint8_t> dds;
    if(TpfExtractDDS(tpfPath, texName, dds) && LoadDDSBytes(dds, rgba, W, H)) return true;
    return false;
}

// Alpha-weighted box downscale of a source rect into an SZ x SZ RGBA icon.
static void ExtractIcon(const std::vector<uint8_t>& src, int W, int H, const IcoDef& r, std::vector<uint8_t>& out){
    out.assign((size_t)ICO_SZ*ICO_SZ*4, 0);
    int sw=r.x1-r.x0, sh=r.y1-r.y0; if(sw<=0||sh<=0) return;
    for(int dy=0;dy<ICO_SZ;++dy) for(int dx=0;dx<ICO_SZ;++dx){
        int sx0=r.x0+dx*sw/ICO_SZ, sx1=r.x0+(dx+1)*sw/ICO_SZ; if(sx1<=sx0)sx1=sx0+1;
        int sy0=r.y0+dy*sh/ICO_SZ, sy1=r.y0+(dy+1)*sh/ICO_SZ; if(sy1<=sy0)sy1=sy0+1;
        long ar=0,ag=0,ab=0,aa=0,n=0;
        for(int sy=sy0;sy<sy1;++sy) for(int sx=sx0;sx<sx1;++sx){
            if(sx<0||sy<0||sx>=W||sy>=H) continue; size_t o=((size_t)sy*W+sx)*4; int al=src[o+3];
            ar+=(long)src[o]*al; ag+=(long)src[o+1]*al; ab+=(long)src[o+2]*al; aa+=al; n++;
        }
        size_t o=((size_t)dy*ICO_SZ+dx)*4;
        if(aa>0){ out[o]=(uint8_t)(ar/aa); out[o+1]=(uint8_t)(ag/aa); out[o+2]=(uint8_t)(ab/aa); out[o+3]=(uint8_t)(aa/(n>0?n:1)); }
    }
}

static uint32_t IconSignature(const std::string& dataDir){
    uint32_t hsh=2166136261u;
    auto mix=[&](const void* pp,size_t n){ const uint8_t* p=(const uint8_t*)pp; for(size_t i=0;i<n;++i){ hsh^=p[i]; hsh*=16777619u; } };
    auto stat=[&](const std::string& path){ uint32_t lo,hi; uint64_t ft;
        if(plat::StatForSignature(path,lo,hi,ft)){ mix(&lo,4); mix(&ft,8); } };
    std::string m0=dataDir+"menu\\menu_0-tpf\\", m1=dataDir+"menu\\menu_1-tpf\\";
    stat(m0+"Icon20.dds"); stat(m0+"Icon40.dds"); stat(m0+"Icon30.dds"); stat(m1+"Menu04_2.dds");  // loose (if unpacked)
    stat(dataDir+"menu\\menu_0.tpf"); stat(dataDir+"menu\\menu_1.tpf");                                // packed (normal layout)
    hsh ^= ICO_VERSION*2654435761u; return hsh;
}

static void GenerateIconCacheIfNeeded(const std::string& dataDir, const std::string& blobPath){
    std::string menuDir  = dataDir + "menu\\menu_0-tpf\\";   // loose DDS if this install unpacked the TPFs
    std::string menu1Dir = dataDir + "menu\\menu_1-tpf\\";   // Menu04_2 (framed item tiles) lives here
    std::string tpf0 = dataDir + "menu\\menu_0.tpf";         // packed source (Icon atlases)
    std::string tpf1 = dataDir + "menu\\menu_1.tpf";         // packed source (Menu screens)
    uint32_t sig = IconSignature(dataDir);
    std::vector<uint8_t> cur = dsfont::ReadAll(blobPath);
    if(cur.size()>=16 && memcmp(cur.data(),"MCTDEICO",8)==0
       && dsfont::rdU32(&cur[8])==ICO_VERSION && dsfont::rdU32(&cur[12])==sig)
        return; // up to date

    std::vector<uint8_t> a20, a40, a30, aM042; int w20=0,h20=0,w40=0,h40=0,w30=0,h30=0,wM042=0,hM042=0;
    bool ok20 = LoadAtlas(menuDir+"Icon20.dds",   tpf0, "Icon20",   a20, w20, h20);
    bool ok40 = LoadAtlas(menuDir+"Icon40.dds",   tpf0, "Icon40",   a40, w40, h40);
    bool ok30 = LoadAtlas(menuDir+"Icon30.dds",   tpf0, "Icon30",   a30, w30, h30);
    bool okM042 = LoadAtlas(menu1Dir+"Menu04_2.dds", tpf1, "Menu04_2", aM042, wM042, hM042);
    if(!ok20) return;   // Icon20 required; Icon40 / Icon30 / Menu04_2 optional

    std::vector<uint8_t> blob; blob.reserve((size_t)ICO_COUNT*ICO_SZ*ICO_SZ*4 + 64);
    auto pushN=[&](const void* p,size_t n){ const uint8_t* b=(const uint8_t*)p; blob.insert(blob.end(),b,b+n); };
    auto pU32=[&](uint32_t v){ pushN(&v,4); }; auto pU16=[&](uint16_t v){ pushN(&v,2); };
    pushN("MCTDEICO",8); pU32(ICO_VERSION); pU32(sig); pU16((uint16_t)ICO_SZ); pU16((uint16_t)ICO_COUNT);
    for(int i=0;i<ICO_COUNT;++i){
        std::vector<uint8_t> ic;
        if(ICO[i].atlas==0)                  ExtractIcon(a20,w20,h20,ICO[i],ic);
        else if(ICO[i].atlas==1 && ok40)     ExtractIcon(a40,w40,h40,ICO[i],ic);
        else if(ICO[i].atlas==2 && ok30)     ExtractIcon(a30,w30,h30,ICO[i],ic);
        else if(ICO[i].atlas==3 && okM042)   ExtractIcon(aM042,wM042,hM042,ICO[i],ic);
        else                                 ic.assign((size_t)ICO_SZ*ICO_SZ*4,0);   // source atlas missing -> blank
        pU16((uint16_t)ICO_SZ); pU16((uint16_t)ICO_SZ);
        pushN(ic.data(), ic.size());
    }
    plat::WriteAtomic(blobPath, blob.data(), blob.size());
}

} // namespace dsicons

// ============================================================ HP gauge frame cache (Souls-style bar)
// Bakes the game's OWN segmented HP-gauge frame out of menu\menu_1-tpf\Menu08.dds into
// mctde_dsgauge.bin, which the overlay blits over its red fill so the phantom-list HP bar matches the
// in-game gauge (black ruler frame: divider + minor ticks + a bold right end-cap). Two RGBA slices are
// stored: a 52x18 middle tile that tiles horizontally at native scale (its own left edge is a divider,
// so it doubles as the left border), and an 8x18 right end-cap. Slice rects were measured pixel-exact
// from the atlas. Generated from the user's own files -- nothing copyrighted ships.
namespace dsgauge {

static const uint32_t GAU_VERSION = 4;
struct Rect { int x, y, w, h; };
// The HP-gauge ruler frame is the finer band at y438-451 (NOT the coarser one lower in the atlas).
// The tile starts at y438 (the solid top bar); y437 above it is a speckly dashed row -- excluded so
// no semi-transparent junk hangs above the bar.
static const Rect GAU_TILE = { 124, 438, 51, 14 };  // divider + 4 minor ticks + continuous top/bottom bars
static const Rect GAU_CAP  = { 123, 438,  4, 14 };  // clean divider closing the bar's right edge

// Copy an RGBA sub-rect out of a decoded atlas (src is W*H RGBA).
static void ExtractRect(const std::vector<uint8_t>& src, int W, int H, const Rect& r, std::vector<uint8_t>& out){
    out.assign((size_t)r.w*r.h*4, 0);
    for(int j=0;j<r.h;++j) for(int i=0;i<r.w;++i){
        int sx=r.x+i, sy=r.y+j; if(sx<0||sy<0||sx>=W||sy>=H) continue;
        size_t s=((size_t)sy*W+sx)*4, d=((size_t)j*r.w+i)*4;
        out[d]=src[s]; out[d+1]=src[s+1]; out[d+2]=src[s+2]; out[d+3]=src[s+3];
    }
}

static uint32_t GaugeSignature(const std::string& dataDir){
    uint32_t hsh=2166136261u;
    auto mix=[&](const void* pp,size_t n){ const uint8_t* p=(const uint8_t*)pp; for(size_t i=0;i<n;++i){ hsh^=p[i]; hsh*=16777619u; } };
    auto stat=[&](const std::string& path){ uint32_t lo,hi; uint64_t ft;
        if(plat::StatForSignature(path,lo,hi,ft)){ mix(&lo,4); mix(&ft,8); } };
    stat(dataDir+"menu\\menu_1-tpf\\Menu08.dds");   // loose (if unpacked)
    stat(dataDir+"menu\\menu_1.tpf");               // packed (normal layout)
    hsh ^= GAU_VERSION*2654435761u; return hsh;
}

static void GenerateGaugeCacheIfNeeded(const std::string& dataDir, const std::string& blobPath){
    uint32_t sig = GaugeSignature(dataDir);
    std::vector<uint8_t> cur = dsfont::ReadAll(blobPath);
    if(cur.size()>=16 && memcmp(cur.data(),"MCTDEGAU",8)==0
       && dsfont::rdU32(&cur[8])==GAU_VERSION && dsfont::rdU32(&cur[12])==sig)
        return; // up to date

    std::vector<uint8_t> atlas; int W=0,H=0;
    if(!dsicons::LoadAtlas(dataDir+"menu\\menu_1-tpf\\Menu08.dds", dataDir+"menu\\menu_1.tpf", "Menu08", atlas, W, H)) return;
    if(GAU_TILE.x+GAU_TILE.w>W || GAU_TILE.y+GAU_TILE.h>H ||
       GAU_CAP.x+GAU_CAP.w>W || GAU_CAP.y+GAU_CAP.h>H) return;

    std::vector<uint8_t> tile, cap;
    ExtractRect(atlas, W, H, GAU_TILE, tile);
    ExtractRect(atlas, W, H, GAU_CAP,  cap);

    std::vector<uint8_t> blob; blob.reserve(tile.size()+cap.size()+64);
    auto pushN=[&](const void* p,size_t n){ const uint8_t* b=(const uint8_t*)p; blob.insert(blob.end(),b,b+n); };
    auto pU32=[&](uint32_t v){ pushN(&v,4); }; auto pU16=[&](uint16_t v){ pushN(&v,2); };
    pushN("MCTDEGAU",8); pU32(GAU_VERSION); pU32(sig);
    pU16((uint16_t)GAU_TILE.w); pU16((uint16_t)GAU_TILE.h);
    pU16((uint16_t)GAU_CAP.w);  pU16((uint16_t)GAU_CAP.h);
    pushN(tile.data(), tile.size());
    pushN(cap.data(),  cap.size());

    plat::WriteAtomic(blobPath, blob.data(), blob.size());
}

} // namespace dsgauge

namespace caches {
void GenerateFontCacheIfNeeded(const std::string& d, const std::string& b)  { dsfont::GenerateFontCacheIfNeeded(d, b); }
void GenerateIconCacheIfNeeded(const std::string& d, const std::string& b)  { dsicons::GenerateIconCacheIfNeeded(d, b); }
void GenerateGaugeCacheIfNeeded(const std::string& d, const std::string& b) { dsgauge::GenerateGaugeCacheIfNeeded(d, b); }
} // namespace caches
