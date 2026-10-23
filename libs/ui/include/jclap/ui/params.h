#pragma once

// What the GUI needs from the plugin to read and edit params. Doesn't include Visage.
//
// ParamAccess is implemented by the plugin and called on the main thread, by CLAP param id. A
// typical implementation stores the value and forwards the gesture to a jclap::core::EditQueue:
//
//    beginEdit(id)     -> edits.begin(id)
//    setParam(id, v)   -> values[id] = v; edits.value(id, v)
//    endEdit(id)       -> edits.end(id)

#include <clap/clap.h>

#include <string>
#include <vector>

namespace jclap::ui {

// The host's own menu for a parameter (automation, MIDI learn...), through CLAP's context-menu
// extension. Widgets bound with ParamBindings append it to their right-click menu.
class HostMenu {
public:
   struct Item {
      enum class Kind { Entry, Check, Separator, BeginSubmenu, EndSubmenu, Title } kind;
      std::string label;
      bool enabled = true;
      bool checked = false;
      clap_id action = 0;
   };

   void init(const clap_host_t *host);
   bool available() const { return menu_ != nullptr; }
   // The host's items for a param, flattened; submenus are bracketed by BeginSubmenu/EndSubmenu.
   std::vector<Item> items(clap_id param) const;
   void perform(clap_id param, clap_id action) const;

private:
   const clap_host_t *host_ = nullptr;
   const clap_host_context_menu_t *menu_ = nullptr;
};

class ParamAccess {
public:
   virtual ~ParamAccess() = default;
   // The param's own value, as the host sees it.
   virtual double paramValue(clap_id id) const = 0;
   // The value in use, with host modulation added. Defaults to paramValue().
   virtual double paramModulated(clap_id id) const { return paramValue(id); }
   // One user gesture: beginEdit, any number of setParam, endEdit.
   virtual void beginEdit(clap_id id) = 0;
   virtual void setParam(clap_id id, double value) = 0;
   virtual void endEdit(clap_id id) = 0;
   // The host's param menu, if the plugin offers it.
   virtual const HostMenu *hostMenu() const { return nullptr; }
};

} // namespace jclap::ui
