// linux/settings.h -- the launcher's tabbed Settings window (Qt), ported from the Windows
// launcher's OpenSettings: a DSFix tab that round-trips dsfix.ini and an mctde tab that round-trips
// mctde-link.ini (overlay placement / font / live preview, then the data-driven settings table),
// with one shared Save / Cancel bar. Both write the same keys the Windows launcher writes.
#pragma once

#include "../core/install.h"

class QWidget;

// initialTab: 0 = DSFix, 1 = mctde (auto-corrected when that tab's ini is missing). Modal.
// Returns true when the player clicked Save.
bool OpenSettings(QWidget* parent, const mctde::Install& inst, int initialTab);
