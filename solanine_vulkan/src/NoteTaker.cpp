#include "pch.h"

#include "NoteTaker.h"

#include "RenderObject.h"
#include "DataSerialization.h"
#include "StringHelper.h"


NoteTaker::NoteTaker(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _rom(rom)
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

NoteTaker::~NoteTaker()
{
    _rom->unregisterRenderObjects({ _renderObj });
    _rom->removeModelCallbacks(this);
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

void NoteTaker::teleportToPosition(vec3 position)
{
    auto& t = _renderObj->transformMatrix;
    mat4 rot;
    vec3 sca;
    glm_decompose_rs(t, rot, sca);

    glm_mat4_identity(t);
    glm_translate(t, position);
    glm_mul_rot(t, rot, t);
    glm_scale(t, sca);
}

void NoteTaker::renderImGui()
{
    ImGui::Text("Notes:");
    ImGui::InputTextMultiline("##NoteTaker notes textarea", &_notes, ImVec2(512, ImGui::GetTextLineHeight() * 16), ImGuiInputTextFlags_AllowTabInput);
}
