#include "ui/Icons.hpp"

namespace coeus {

QIcon Icons::themed(std::initializer_list<const char*> themeNames, const char* resource)
{
    for (const char* name : themeNames) {
        if (QIcon::hasThemeIcon(QLatin1String(name)))
            return QIcon::fromTheme(QLatin1String(name));
    }
    return QIcon(QLatin1String(resource));
}

QIcon Icons::explorer()
{
    return themed({"folder"}, ":/icons/explorer.svg");
}

QIcon Icons::search()
{
    return themed({"edit-find"}, ":/icons/search.svg");
}

QIcon Icons::board()
{
    return themed({"view-list-details", "x-office-spreadsheet"}, ":/icons/board.svg");
}

QIcon Icons::tags()
{
    return themed({"tag", "bookmarks"}, ":/icons/tags.svg");
}

QIcon Icons::palette()
{
    return themed({"system-search"}, ":/icons/palette.svg");
}

QIcon Icons::sidebar()
{
    // Not view-list-details: the board may have taken it, and two buttons would look alike.
    return themed({"view-right-pane", "sidebar-show-right"}, ":/icons/sidebar.svg");
}

} // namespace coeus
