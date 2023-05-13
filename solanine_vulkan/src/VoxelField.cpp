#include "VoxelField.h"


struct VoxelField_XData
{
    RenderObjectManager* rom;

    //
    // @NOTE: VOXEL DATA GUIDE
    //
    // Will implement:
    //        0: empty space
    //        1: filled space
    //
    // For the future:
    //        2: half-height space (bottom)
    //        3: half-height space (top)
    //        4: 4-step stair space (South 0.0 height, North 0.5 height)
    //        5: 4-step stair space (South 0.5 height, North 1.0 height)
    //        6: 4-step stair space (North 0.0 height, South 0.5 height)
    //        7: 4-step stair space (North 0.5 height, South 1.0 height)
    //        8: 4-step stair space (East  0.0 height, West  0.5 height)
    //        9: 4-step stair space (East  0.5 height, West  1.0 height)
    //       10: 4-step stair space (West  0.0 height, East  0.5 height)
    //       11: 4-step stair space (West  0.5 height, East  1.0 height)
    //
    size_t sizeX = 8,
           sizeY = 8,
           sizeZ = 8;
    uint8_t* voxelData;
};

inline void    buildVoxelData(VoxelField_XData& data);
inline uint8_t getVoxelDataAtPosition(VoxelField_XData& data, const int32_t& x, const int32_t& y, const int32_t& z);
inline void    assembleVoxelRenderObjects(VoxelField_XData& data);


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
    buildVoxelData(*_data);
    assembleVoxelRenderObjects(*_data);
}

VoxelField::~VoxelField()
{
    delete _data->voxelData;
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

}

void VoxelField::renderImGui()
{

}


inline void buildVoxelData(VoxelField_XData& data)
{
    uint8_t* vd = new uint8_t[data.sizeX * data.sizeY * data.sizeZ];
    for (size_t i = 0; i < data.sizeX; i++)
    for (size_t j = 0; j < data.sizeY; j++)
    for (size_t k = 0; k < data.sizeZ; k++)
        vd[i * data.sizeY * data.sizeZ + j * data.sizeZ + k] = 1;
    data.voxelData = vd;
}

inline uint8_t getVoxelDataAtPosition(VoxelField_XData& data, const int32_t& x, const int32_t& y, const int32_t& z)
{
    if (x < 0 || y < 0 || z < 0 ||
        x >= data.sizeX || y >= data.sizeY || z >= data.sizeZ)
        return 0;
    return data.voxelData[(size_t)x * data.sizeY * data.sizeZ + (size_t)y * data.sizeZ + (size_t)z];
}

inline void assembleVoxelRenderObjects(VoxelField_XData& data)
{
    // Check for if voxel is filled and not surrounded
    for (int32_t i = 0; i < data.sizeX; i++)
    for (int32_t j = 0; j < data.sizeY; j++)
    for (int32_t k = 0; k < data.sizeZ; k++)
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
                addRenderObjectAtPosition(i, j, k);  // @TODO
        }
    }
}