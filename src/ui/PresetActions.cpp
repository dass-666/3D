#include "ui/PresetActions.h"

#include "model/Scene.h"

namespace ses::presets
{
namespace
{
juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
}

std::vector<Entry> all()
{
    std::vector<Entry> out;
    for (const auto& cp : pointPresets())
        out.push_back ({ cp.id, u8 (cp.name), u8 (cp.description), false });
    for (const auto& up : UserPresetStore::shared().presets())
        out.push_back ({ up.id, up.name, up.description, true });
    return out;
}

bool applyToSelection (EditorContext& ctx, const juce::String& id)
{
    auto ids = ctx.selection();
    if (ids.isEmpty())
        if (const auto* p = sourcePoint (ctx))
            ids.add (p->id);
    if (ids.isEmpty())
        return false;
    std::function<void (Point&)> fn;
    if (const auto* up = UserPresetStore::shared().find (id))
        fn = [up = *up] (Point& p) { applyUserPreset (p, up); };
    else
        for (const auto& cp : pointPresets())
            if (id == cp.id)
                fn = [&cp] (Point& p) { applyPointPreset (p, cp); };
    if (! fn)
        return false;
    ctx.edit ([&] (Scene& s)
    {
        for (auto& pid : ids)
            if (auto* p = findPoint (s, pid))
                fn (*p);
    });
    return true;
}

const Point* sourcePoint (const EditorContext& ctx)
{
    if (const auto* p = ctx.primaryPoint())
        return p;
    const auto& pts = ctx.scene().points;
    return pts.empty() ? nullptr : &pts.front();
}

juce::String saveFromSelection (EditorContext& ctx, const juce::String& name)
{
    const auto* p = sourcePoint (ctx);
    if (p == nullptr)
        return {};
    auto& store = UserPresetStore::shared();
    const auto pointId = p->id;
    const auto id = store.addFromPoint (name.isEmpty() ? u8 ("Mi preset") : name, u8 ("Preset propio"), *p);
    if (id.isEmpty() || store.find (id) == nullptr)
        return {};
    ctx.edit ([&] (Scene& s) { if (auto* q = findPoint (s, pointId)) q->creativePreset = id; });
    return id;
}

void askNameAndSave (EditorContext& ctx, juce::Component& parent, std::function<void (const juce::String&)> done)
{
    const auto* p = sourcePoint (ctx);
    if (p == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Guardar preset",
                                                u8 ("A\xc3\xb1" "ade o selecciona una repetici\xc3\xb3n primero: el preset guarda su sonido, sus cortes y sus efectos."),
                                                {}, &parent);
        return;
    }
    const juce::String what = p->id == kLivePointId ? u8 ("los efectos en vivo")
                                                    : u8 ("la repetici\xc3\xb3n ") + juce::String (p->number);
    auto* w = new juce::AlertWindow (u8 ("Guardar preset"),
                                     u8 ("Guarda el sonido, los cortes y la cadena de efectos de ") + what
                                         + u8 (". Lo encontrar\xc3\xa1s en \xc2\xabMis presets\xc2\xbb, en este y en otros proyectos."),
                                     juce::MessageBoxIconType::NoIcon, &parent);
    w->addTextEditor ("name", u8 ("Mi preset"), u8 ("Nombre"));
    w->addButton ("Guardar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    // The callback runs before the window is deleted (deleteWhenDismissed).
    w->enterModalState (true, juce::ModalCallbackFunction::create ([&ctx, w, done] (int r)
    {
        juce::String id;
        if (r == 1)
            id = saveFromSelection (ctx, w->getTextEditorContents ("name").trim());
        if (done)
            done (r == 1 ? (id.isEmpty() ? juce::String ("!") : id) : juce::String());
    }), true);
}

juce::PopupMenu buildMenu (EditorContext& ctx, juce::Component& parent, std::function<void (const juce::String&)> onNote)
{
    auto& store = UserPresetStore::shared();
    const auto list = all();
    const auto* p = sourcePoint (ctx);
    const auto current = p != nullptr ? p->creativePreset : juce::String();
    auto note = [onNote] (const juce::String& t) { if (onNote) onNote (t); };
    juce::Component::SafePointer<juce::Component> safeParent (&parent);

    juce::PopupMenu m;
    m.addSectionHeader (ctx.selection().size() > 1 ? u8 ("Aplicar a las repeticiones seleccionadas")
                                                   : u8 ("Aplicar a la repetici\xc3\xb3n seleccionada"));
    auto addList = [&] (juce::PopupMenu& menu, auto filter)
    {
        int added = 0;
        for (const auto& e : list)
            if (filter (e))
            {
                ++added;
                menu.addItem ((store.isFavourite (e.id) ? u8 ("\xe2\x98\x85 ") : juce::String()) + e.name, p != nullptr, e.id == current,
                              [&ctx, id = e.id, name = e.name, note]
                              {
                                  if (applyToSelection (ctx, id))
                                      note (u8 ("Preset \xc2\xab") + name + u8 ("\xc2\xbb aplicado"));
                              });
            }
        return added;
    };
    juce::PopupMenu favs, mine, builtIn;
    const int nFav = addList (favs, [&] (const Entry& e) { return store.isFavourite (e.id); });
    const int nMine = addList (mine, [&] (const Entry& e) { return e.user; });
    addList (builtIn, [&] (const Entry& e) { return ! e.user; });
    if (nMine == 0)
        mine.addItem (u8 ("(todav\xc3\xad" "a no hay presets propios)"), false, false, [] {});
    m.addSubMenu (u8 ("Mis presets (") + juce::String (nMine) + ")", mine);
    if (nFav > 0)
        m.addSubMenu (u8 ("Favoritos (") + juce::String (nFav) + ")", favs);
    m.addSubMenu ("Integrados", builtIn);

    m.addSeparator();
    m.addItem (u8 ("Guardar preset\xe2\x80\xa6"), p != nullptr, false, [&ctx, safeParent, note]
    {
        if (safeParent != nullptr)
            askNameAndSave (ctx, *safeParent, [note] (const juce::String& id)
            {
                if (id == "!")
                    note (u8 ("No se pudo guardar el preset: ") + UserPresetStore::shared().lastError);
                else if (id.isNotEmpty())
                    if (const auto* up = UserPresetStore::shared().find (id))
                        note (u8 ("Preset \xc2\xab") + up->name + u8 ("\xc2\xbb guardado en Mis presets"));
            });
    });
    if (current.isNotEmpty())
    {
        const bool fav = store.isFavourite (current);
        m.addItem (fav ? u8 ("Quitar el preset actual de favoritos") : u8 ("Marcar el preset actual como favorito"), true, false,
                   [current, fav] { UserPresetStore::shared().setFavourite (current, ! fav); });
    }
    juce::PopupMenu del;
    for (const auto& e : list)
        if (e.user)
            del.addItem (e.name, true, false, [id = e.id, name = e.name, note]
            {
                if (UserPresetStore::shared().remove (id))
                    note (u8 ("Preset \xc2\xab") + name + u8 ("\xc2\xbb eliminado"));
            });
    m.addSubMenu (u8 ("Eliminar un preset propio"), del, nMine > 0);
    m.addSeparator();
    const auto file = UserPresetStore::defaultFile();
    m.addItem (u8 ("Abrir la carpeta de presets"), true, false, [file]
    {
        if (file.existsAsFile())
            file.revealToUser();
        else
            file.getParentDirectory().startAsProcess();
    });
    if (store.loadFailed())
        m.addItem (u8 ("\xe2\x9a\xa0 El archivo de presets no se pudo leer (copia en presets-ilegible.json)"), false, false, [] {});
    return m;
}

} // namespace ses::presets
