#pragma once

#include <QString>

namespace mothion::io {

// The Blender program, if it can be found: the one asked for (when it exists), then the search path, then the places Blender
// installs itself to; empty if not.
QString findBlender(const QString &preferred = {});

// Installs the Mothion Link add-on (blender/mothion_link, kept inside the program) into a Blender, as an extension of the user's
// own, and turns it on, through the program's command line (a Blender that is open has to be started again, or the add-on
// turned on in its preferences). True if it worked; `message` says what happened either way.
bool installBlenderAddOn(const QString &blender, QString *message);

} // namespace mothion::io
