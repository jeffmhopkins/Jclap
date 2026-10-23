#include "jclap/ui/params.h"

namespace jclap::ui {

void HostMenu::init(const clap_host_t *host) {
   host_ = host;
   auto *menu = static_cast<const clap_host_context_menu_t *>(host->get_extension(host, CLAP_EXT_CONTEXT_MENU));
   if (!menu)
      menu = static_cast<const clap_host_context_menu_t *>(host->get_extension(host, CLAP_EXT_CONTEXT_MENU_COMPAT));
   if (menu && menu->populate && menu->perform)
      menu_ = menu;
}

std::vector<HostMenu::Item> HostMenu::items(clap_id param) const {
   std::vector<Item> items;
   if (!menu_)
      return items;

   const auto addItem = [](const clap_context_menu_builder_t *builder, clap_context_menu_item_kind_t kind,
                           const void *data) {
      auto &list = *static_cast<std::vector<Item> *>(builder->ctx);
      Item item{};
      switch (kind) {
      case CLAP_CONTEXT_MENU_ITEM_ENTRY: {
         auto *e = static_cast<const clap_context_menu_entry_t *>(data);
         item = {Item::Kind::Entry, e->label ? e->label : "", e->is_enabled, false, e->action_id};
         break;
      }
      case CLAP_CONTEXT_MENU_ITEM_CHECK_ENTRY: {
         auto *e = static_cast<const clap_context_menu_check_entry_t *>(data);
         item = {Item::Kind::Check, e->label ? e->label : "", e->is_enabled, e->is_checked, e->action_id};
         break;
      }
      case CLAP_CONTEXT_MENU_ITEM_SEPARATOR:
         item.kind = Item::Kind::Separator;
         break;
      case CLAP_CONTEXT_MENU_ITEM_BEGIN_SUBMENU: {
         auto *e = static_cast<const clap_context_menu_submenu_t *>(data);
         item = {Item::Kind::BeginSubmenu, e->label ? e->label : "", e->is_enabled, false, 0};
         break;
      }
      case CLAP_CONTEXT_MENU_ITEM_END_SUBMENU:
         item.kind = Item::Kind::EndSubmenu;
         break;
      case CLAP_CONTEXT_MENU_ITEM_TITLE: {
         auto *e = static_cast<const clap_context_menu_item_title_t *>(data);
         item = {Item::Kind::Title, e->title ? e->title : "", e->is_enabled, false, 0};
         break;
      }
      default:
         return false;
      }
      list.push_back(std::move(item));
      return true;
   };
   const auto supports = [](const clap_context_menu_builder_t *, clap_context_menu_item_kind_t kind) {
      return kind <= CLAP_CONTEXT_MENU_ITEM_TITLE;
   };

   const clap_context_menu_builder_t builder = {&items, addItem, supports};
   const clap_context_menu_target_t target = {CLAP_CONTEXT_MENU_TARGET_KIND_PARAM, param};
   menu_->populate(host_, &target, &builder);
   return items;
}

void HostMenu::perform(clap_id param, clap_id action) const {
   if (!menu_)
      return;
   const clap_context_menu_target_t target = {CLAP_CONTEXT_MENU_TARGET_KIND_PARAM, param};
   menu_->perform(host_, &target, action);
}

} // namespace jclap::ui
