#include "EDITORTextureViewer.h"

#include "RenderObject.h"
#include "DataSerialization.h"
#include "StringHelper.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"


EDITORTextureViewer::EDITORTextureViewer(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _rom(rom)
{
    if (ds)
        load(*ds);

    vkglTF::Model* model = _rom->getModel("NotesIcon", this, [](){});
    _rom->registerRenderObjects({
            {
                .model = model,
                .renderLayer = RenderLayer::BUILDER,
                .attachedEntityGuid = getGUID(),
            }
        },
        { &_renderObj }
    );
    glm_mat4_copy(_load_transform, _renderObj->transformMatrix);
}

EDITORTextureViewer::~EDITORTextureViewer()
{
    _rom->unregisterRenderObjects({ _renderObj });
    _rom->removeModelCallbacks(this);
}

void EDITORTextureViewer::dump(DataSerializer& ds)
{
    Entity::dump(ds);

    ds.dumpMat4(_renderObj->transformMatrix);

    std::string sc = _notes;
    replaceAll(sc, "\n", "\\n");
    ds.dumpString(sc);
}

void EDITORTextureViewer::load(DataSerialized& ds)
{
    Entity::load(ds);
    
    ds.loadMat4(_load_transform);

    size_t numLines = ds.getSerializedValuesCount();
    for (size_t i = 0; i < numLines; i++)
    {
        if (i > 0)
            _notes += "\n";
        std::string s;
        ds.loadString(s);
        _notes += s;
    }
    replaceAll(_notes, "\\n", "\n");
}

void EDITORTextureViewer::renderImGui()
{
    ImGui::Text("Notes:");
    ImGui::InputTextMultiline("##EDITORTextureViewer notes textarea", &_notes, ImVec2(512, ImGui::GetTextLineHeight() * 16), ImGuiInputTextFlags_AllowTabInput);
}
