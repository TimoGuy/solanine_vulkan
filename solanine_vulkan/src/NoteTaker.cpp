#include "NoteTaker.h"

#include "RenderObject.h"
#include "DataSerialization.h"
#include "StringHelper.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"


NoteTaker::NoteTaker(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _rom(rom)
{
    if (ds)
        load(*ds);

    vkglTF::Model* model = _rom->getModel("NotesIcon");
    _renderObj =
        _rom->registerRenderObject({
            .model = model,
            .transformMatrix = _load_transform,
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
        });
}

NoteTaker::~NoteTaker()
{
    _rom->unregisterRenderObject(_renderObj);
}

void NoteTaker::dump(DataSerializer& ds)
{
    Entity::dump(ds);

    ds.dumpMat4(_renderObj->transformMatrix);

    std::string sc = _notes;
    replaceAll(sc, "\n", "\\n");
    ds.dumpString(sc);
}

void NoteTaker::load(DataSerialized& ds)
{
    Entity::load(ds);
    
    _load_transform = ds.loadMat4();

    size_t numLines = ds.getSerializedValuesCount();
    for (size_t i = 0; i < numLines; i++)
    {
        if (i > 0)
            _notes += "\n";
        _notes += ds.loadString();
    }
    replaceAll(_notes, "\\n", "\n");
}

void NoteTaker::renderImGui()
{
    ImGui::InputTextMultiline("Notes", &_notes, ImVec2(0, 0), ImGuiInputTextFlags_AllowTabInput);
}
