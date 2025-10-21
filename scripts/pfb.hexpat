import std.io;
import std.core;

struct ivec3
{
    s32 x, y, z;
} [[format("FormatIvec3")]];

fn FormatIvec3(ref ivec3 v)
{
    return std::format("{}, {}, {}", v.x, v.y, v.z);
};

struct pair
{
    ivec3 pos;
    u32 voxel;
} [[format("FormatPair")]];

fn FormatPair(ref pair p)
{
    return std::format("{}, {}, {}: {}", p.pos.x, p.pos.y, p.pos.z, p.voxel);
};

struct Thingy
{
    u32 size;
    pair arr[while(!std::mem::eof())];
};

Thingy a @ 0;