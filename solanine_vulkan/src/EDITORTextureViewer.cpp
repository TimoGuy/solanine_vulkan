#include "pch.h"

#include "EDITORTextureViewer.h"

#include "RenderObject.h"
#include "Camera.h"
#include "DataSerialization.h"


struct EDITORTextureViewer_XData
{
    RenderObjectManager* rom;
    RenderObject*        renderObj;

    size_t currentAssignedUMB = (size_t)-1;
    size_t currentAssignedDMPS = (size_t)-1;
};

size_t INTERNAL_EDITORTEXTUREVIEWER_assignedMaterialUMB = 0;
size_t INTERNAL_EDITORTEXTUREVIEWER_assignedMaterialDMPS = 0;


EDITORTextureViewer::EDITORTextureViewer(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), d(new EDITORTextureViewer_XData)
{
    Entity::_enableSimulationUpdate = true;

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

void EDITORTextureViewer::simulationUpdate(float_t simDeltaTime)
{
    if (d->currentAssignedUMB != INTERNAL_EDITORTEXTUREVIEWER_assignedMaterialUMB)
    {
        d->currentAssignedUMB = INTERNAL_EDITORTEXTUREVIEWER_assignedMaterialUMB;
        for (auto& umbi : d->renderObj->perPrimitiveUniqueMaterialBaseIndices)
            umbi = d->currentAssignedUMB;
    }
    if (d->currentAssignedDMPS != INTERNAL_EDITORTEXTUREVIEWER_assignedMaterialDMPS)
    {
        d->currentAssignedDMPS = INTERNAL_EDITORTEXTUREVIEWER_assignedMaterialDMPS;
        for (auto& cmi : d->renderObj->calculatedModelInstances)
            cmi.materialID = d->currentAssignedDMPS;
    }
}

void EDITORTextureViewer::dump(DataSerializer& ds)
{
    Entity::dump(ds);
}

void EDITORTextureViewer::load(DataSerialized& ds)
{
    Entity::load(ds);
}

void EDITORTextureViewer::setAssignedMaterial(size_t uniqueMatBaseId, size_t derivedMatId)
{
    INTERNAL_EDITORTEXTUREVIEWER_assignedMaterialUMB = uniqueMatBaseId;
    INTERNAL_EDITORTEXTUREVIEWER_assignedMaterialDMPS = derivedMatId;
}
