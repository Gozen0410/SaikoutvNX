from pathlib import Path

root = Path("switch/borealis")
box_h = root / "library/include/borealis/core/box.hpp"
box_cpp = root / "library/lib/core/box.cpp"
tab_h = root / "library/include/borealis/views/tab_frame.hpp"
tab_cpp = root / "library/lib/views/tab_frame.cpp"

for p in (box_h, box_cpp, tab_h, tab_cpp):
    if not p.exists():
        raise SystemExit(f"Missing Borealis source: {p}")

# Add a non-destructive detach primitive. The existing removeView() owns and
# deletes the child, which is exactly what makes the dynamically populated Home
# disappear when the sidebar moves to another tab.
s = box_h.read_text()
if "void detachView(View* view);" not in s:
    marker = "    virtual void removeView(View* view);\n"
    if s.count(marker) != 1:
        raise SystemExit("Box removeView declaration not found")
    s = s.replace(marker, marker + "    void detachView(View* view);\n", 1)
    box_h.write_text(s)

s = box_cpp.read_text()
if "void Box::detachView(View* view)" not in s:
    marker = "void Box::removeView(View* view)\n"
    if s.count(marker) != 1:
        raise SystemExit("Box removeView definition not found")
    method = '''void Box::detachView(View* view)
{
    if (!view)
        return;

    size_t index = 0;
    bool found = false;
    for (size_t i = 0; i < this->children.size(); ++i)
    {
        if (this->children[i] == view)
        {
            index = i;
            found = true;
            break;
        }
    }
    if (!found)
        return;

    YGNodeRemoveChild(this->ygNode, view->getYGNode());
    this->children.erase(this->children.begin() + index);
    view->willDisappear(false);
    void* userdata = view->getParentUserData();
    view->setParent(nullptr, nullptr);
    std::free(userdata);
    this->invalidate();
}

'''
    s = s.replace(marker, method + marker, 1)
    box_cpp.write_text(s)

# Cache ONLY Home. Other tabs keep Borealis' original destroy/recreate
# semantics, so the API Source activity/navigation remains untouched.
s = tab_h.read_text()
if "~TabFrame() override;" not in s:
    s = s.replace("    TabFrame();\n", "    TabFrame();\n    ~TabFrame() override;\n", 1)
if "View* cachedHomeTab = nullptr;" not in s:
    marker = "    View* activeTab = nullptr;\n"
    if s.count(marker) != 1:
        raise SystemExit("TabFrame activeTab member not found")
    s = s.replace(marker, marker + "    View* cachedHomeTab = nullptr;\n", 1)
tab_h.write_text(s)

s = tab_cpp.read_text()
start = s.find("void TabFrame::addTab(std::string label, TabViewCreator creator)\n")
end = s.find("\nvoid TabFrame::addSeparator()", start)
if start < 0 or end < 0:
    raise SystemExit("TabFrame addTab boundaries not found")

add_tab = '''void TabFrame::addTab(std::string label, TabViewCreator creator)
{
    this->sidebar->addItem(label, [this, creator, label](brls::View* view) {
        if (!view->isFocused())
            return;

        Box* contentView = (Box*)this->contentView;
        if (!contentView)
            return;

        if (this->activeTab)
        {
            if (this->activeTab == this->cachedHomeTab)
                contentView->detachView(this->activeTab);
            else
                contentView->removeView(this->activeTab);
            this->activeTab = nullptr;
        }

        View* newContent = nullptr;
        if (label == "Home" && this->cachedHomeTab)
        {
            newContent = this->cachedHomeTab;
        }
        else
        {
            newContent = creator();
            if (!newContent)
                return;
            if (label == "Home")
                this->cachedHomeTab = newContent;
        }

        View* entryFocus = newContent->getDefaultFocus();
        if (entryFocus)
        {
            view->setCustomNavigationRoute(FocusDirection::RIGHT, entryFocus);
            entryFocus->setCustomNavigationRoute(FocusDirection::LEFT, view);
        }
        else
        {
            newContent->setFocusable(true);
            view->setCustomNavigationRoute(FocusDirection::RIGHT, newContent);
            newContent->setCustomNavigationRoute(FocusDirection::LEFT, view);
        }

        newContent->setGrow(1.0f);
        contentView->addView(newContent);
        this->activeTab = newContent;
    });
}
'''
s = s[:start] + add_tab + s[end:]

# setTabContent() was injected by the existing workflow. Make it replace a
# cached Home instance safely rather than calling removeView() on it.
start = s.find("void TabFrame::setTabContent(View* content)\n")
if start < 0:
    raise SystemExit("TabFrame setTabContent was not injected by workflow")
end = s.find("\nvoid TabFrame::addSeparator()", start)
if end < 0:
    raise SystemExit("TabFrame setTabContent end not found")

set_block = '''void TabFrame::setTabContent(View* content)
{
    if (!content)
        return;

    Box* contentView = (Box*)this->contentView;
    if (!contentView)
        return;

    if (this->activeTab)
    {
        View* old = this->activeTab;
        if (old == this->cachedHomeTab)
            contentView->detachView(old);
        else
            contentView->removeView(old);
        this->activeTab = nullptr;

        if (old == this->cachedHomeTab)
            delete old;
    }

    content->setGrow(1.0f);
    contentView->addView(content);
    this->activeTab = content;
    this->cachedHomeTab = content;
}
'''
s = s[:start] + set_block + s[end:]

if "TabFrame::~TabFrame()" not in s:
    marker = "View* TabFrame::create()\n"
    if s.count(marker) != 1:
        raise SystemExit("TabFrame create definition not found")
    destructor = '''TabFrame::~TabFrame()
{
    if (this->cachedHomeTab && this->cachedHomeTab != this->activeTab)
        delete this->cachedHomeTab;
}

'''
    s = s.replace(marker, destructor + marker, 1)

tab_cpp.write_text(s)
print("Home-only TabFrame caching installed")