#pragma once

#include <QIcon>

namespace coeus {

// Icons for the tool strip: the desktop theme's own when it has one, else
// the one Coeus ships (resources/icons), so every button shows an icon on
// any machine, whatever icon theme is installed (or none).
class Icons {
public:
    static QIcon explorer();
    static QIcon search();
    static QIcon board();
    static QIcon tags();
    static QIcon palette();
    static QIcon sidebar();

    // `themeNames` in order, the first the theme has; else `resource` (":/icons/…").
    static QIcon themed(std::initializer_list<const char*> themeNames, const char* resource);
};

} // namespace coeus
