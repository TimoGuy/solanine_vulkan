#include "VoxelField.h"

#include "Imports.h"
#include "VkglTFModel.h"
#include "RenderObject.h"
#include "PhysicsEngine.h"
#include "imgui/imgui.h"


struct VoxelField_XData
{
    RenderObjectManager* rom;
    vkglTF::Model* voxelModel;
    std::vector<RenderObject*> voxelRenderObjs;
    std::vector<glm::vec3> voxelOffsets;

    physengine::VoxelFieldPhysicsData* vfpd;
};

inline void    buildVoxelData(VoxelField_XData& data);
inline uint8_t getVoxelDataAtPosition(VoxelField_XData& data, const int32_t& x, const int32_t& y, const int32_t& z);
inline void    assembleVoxelRenderObjects(VoxelField_XData& data, const std::string& attachedEntityGuid);
inline void    deleteVoxelRenderObjects(VoxelField_XData& data);


VoxelField::VoxelField(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _data(new VoxelField_XData())
{
    Entity::_enablePhysicsUpdate = true;
    Entity::_enableUpdate = true;
    Entity::_enableLateUpdate = true;

    _data->rom = rom;

    if (ds)
        load(*ds);

    //
    // Initialization
    //
    _data->voxelModel = _data->rom->getModel("DevBoxWood", this, [](){});
    buildVoxelData(*_data);
    assembleVoxelRenderObjects(*_data, getGUID());
}

VoxelField::~VoxelField()
{
    deleteVoxelRenderObjects(*_data);
    physengine::destroyVoxelField(_data->vfpd);
    delete _data;
}

void VoxelField::physicsUpdate(const float_t& physicsDeltaTime)
{

}

void VoxelField::update(const float_t& deltaTime)
{

}

void VoxelField::lateUpdate(const float_t& deltaTime)
{

}

void VoxelField::dump(DataSerializer& ds)
{
    Entity::dump(ds);
}

void VoxelField::load(DataSerialized& ds)
{
    Entity::load(ds);
}

void VoxelField::reportMoved(void* matrixMoved)
{
    // Search for which block was moved
    size_t i = 0;
    for (; i < _data->voxelRenderObjs.size(); i++)
        if (&_data->voxelRenderObjs[i]->transformMatrix == matrixMoved)
            break;
    
    _data->vfpd->transform = glm::translate(*(glm::mat4*)matrixMoved, -_data->voxelOffsets[i]);

    // Move all blocks according to the transform
    for (size_t i2 = 0; i2 < _data->voxelOffsets.size(); i2++)
    {
        if (i2 == i)
            continue;
        _data->voxelRenderObjs[i2]->transformMatrix = glm::translate(_data->vfpd->transform, _data->voxelOffsets[i2]);
    }
}

void VoxelField::renderImGui()
{
    ImGui::Text("Hello there!");
}


inline void buildVoxelData(VoxelField_XData& data)
{
    size_t sizeX = 8, sizeY = 8, sizeZ = 8;
    uint8_t* vd = new uint8_t[sizeX * sizeY * sizeZ];
    for (size_t i = 0; i < sizeX; i++)
    for (size_t j = 0; j < sizeY; j++)
    for (size_t k = 0; k < sizeZ; k++)
        vd[i * sizeY * sizeZ + j * sizeZ + k] = 1;
    data.vfpd = physengine::createVoxelField(sizeX, sizeY, sizeZ, vd);
}

inline uint8_t getVoxelDataAtPosition(VoxelField_XData& data, const int32_t& x, const int32_t& y, const int32_t& z)
{
    if (x < 0                 || y < 0                 || z < 0                ||
        x >= data.vfpd->sizeX || y >= data.vfpd->sizeY || z >= data.vfpd->sizeZ)
        return 0;
    return data.vfpd->voxelData[(size_t)x * data.vfpd->sizeY * data.vfpd->sizeZ + (size_t)y * data.vfpd->sizeZ + (size_t)z];
}

inline void assembleVoxelRenderObjects(VoxelField_XData& data, const std::string& attachedEntityGuid)
{
    deleteVoxelRenderObjects(data);

    // Check for if voxel is filled and not surrounded
    for (int32_t i = 0; i < data.vfpd->sizeX; i++)
    for (int32_t j = 0; j < data.vfpd->sizeY; j++)
    for (int32_t k = 0; k < data.vfpd->sizeZ; k++)
    {
        if (getVoxelDataAtPosition(data, i, j, k))
        {
            // Check if surrounded. If not, add a renderobject for this voxel
            if (!getVoxelDataAtPosition(data, i + 1, j, k) ||
                !getVoxelDataAtPosition(data, i - 1, j, k) ||
                !getVoxelDataAtPosition(data, i, j + 1, k) ||
                !getVoxelDataAtPosition(data, i, j - 1, k) ||
                !getVoxelDataAtPosition(data, i, j, k + 1) ||
                !getVoxelDataAtPosition(data, i, j, k - 1))
            {
                glm::vec3 offset = glm::vec3(i, j, k) + glm::vec3(0.5f, 0.5f, 0.5f);
                RenderObject* newRO =
                    data.rom->registerRenderObject({
                        .model = data.voxelModel,
                        .transformMatrix = glm::translate(data.vfpd->transform, offset),
                        .renderLayer = RenderLayer::VISIBLE,
                        .attachedEntityGuid = attachedEntityGuid,
                    });
                data.voxelRenderObjs.push_back(newRO);
                data.voxelOffsets.push_back(offset);
            }
        }
    }
}

inline void deleteVoxelRenderObjects(VoxelField_XData& data)
{
    for (auto& v : data.voxelRenderObjs)
        data.rom->unregisterRenderObject(v);
    data.voxelRenderObjs.clear();
    data.voxelOffsets.clear();
}
