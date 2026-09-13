from pathlib import Path

path = Path("switch/source/main.cpp")
source = path.read_text()

# Keep this patch isolated from API, Home XML, and TabFrame internals.
# It adds a small controller-driven top-level navigation overlay only when
# the source does not already contain this marker.
if "SAIKOU_TOP_NAV_V1" not in source:
    marker = "class HomeActivity : public brls::Activity"
    pos = source.find(marker)
    if pos < 0:
        raise SystemExit("HomeActivity marker not found")

    nav = r'''
// SAIKOU_TOP_NAV_V1: safe top-level navigation controls.
// These are ordinary Borealis focusable views; no TabFrame internals are used.
class SaikouTopNavigation : public brls::Box
{
public:
    SaikouTopNavigation() : brls::Box(brls::Axis::ROW)
    {
        setWidth(420);
        setHeight(52);
        setGrow(0.0f);
        setShrink(0.0f);
        setAlignItems(brls::AlignItems::CENTER);
        addTab("HOME", 0);
        addTab("BROWSE", 1);
        addTab("SETTINGS", 2);
        selected = 0;
    }

private:
    std::vector<brls::Label*> labels;
    size_t selected = 0;

    void addTab(const char* text, size_t index)
    {
        brls::Label* label = new brls::Label();
        label->setText(text);
        label->setFontSize(16);
        label->setSingleLine(true);
        label->setFocusable(true);
        label->setMargins(8, 14, 8, 14);
        label->registerAction("Select", brls::BUTTON_A, [this, index](brls::View*) {
            activate(index);
            return true;
        });
        label->registerAction("Previous tab", brls::BUTTON_LEFT, [this, index](brls::View*) {
            if (index > 0) select(index - 1);
            return true;
        });
        label->registerAction("Next tab", brls::BUTTON_RIGHT, [this, index](brls::View*) {
            if (index + 1 < labels.size()) select(index + 1);
            return true;
        });
        labels.push_back(label);
        addView(label);
    }

    void select(size_t index)
    {
        if (index >= labels.size()) return;
        selected = index;
        brls::Application::giveFocus(labels[selected]);
    }

    void activate(size_t index)
    {
        if (index >= labels.size()) return;
        selected = index;
        log_stage(index == 0 ? "TOP NAV HOME" : index == 1 ? "TOP NAV BROWSE" : "TOP NAV SETTINGS");

        // Keep this milestone deliberately safe: Home stays untouched while
        // Browse/Settings get a visible placeholder until their real screens exist.
        brls::Application::notify("Home");
        if (index == 1) brls::Application::notify("Browse");
        if (index == 2) brls::Application::notify("Settings");
    }
};

'''
    source = source[:pos] + nav + source[pos:]
    path.write_text(source)
    print("Top navigation patch installed")
else:
    print("Top navigation patch already installed")
