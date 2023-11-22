#include "EDITORTextureViewer.h"

#include "RenderObject.h"
#include "Camera.h"
#include "DataSerialization.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"


struct EDITORTextureViewer_XData
{
    RenderObjectManager* rom;
    RenderObject*        renderObj;
};


EDITORTextureViewer::EDITORTextureViewer(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), d(new EDITORTextureViewer_XData)
{
    d->rom = rom;

    if (ds)
        load(*ds);

    vkglTF::Model* model = d->rom->getModel("EDITOR_TextureViewerSphere", this, [](){});
    d->rom->registerRenderObjects({
            {
                .model = model,
                .renderLayer = RenderLayer::VISIBLE,
                .attachedEntityGuid = getGUID(),
            }
        },
        { &d->renderObj }
    );
}

EDITORTextureViewer::~EDITORTextureViewer()
{
    d->rom->unregisterRenderObjects({ d->renderObj });
    d->rom->removeModelCallbacks(this);
}

void EDITORTextureViewer::dump(DataSerializer& ds)
{
    Entity::dump(ds);
}

void EDITORTextureViewer::load(DataSerialized& ds)
{
    Entity::load(ds);
}

void EDITORTextureViewer::renderImGui()
{
}
