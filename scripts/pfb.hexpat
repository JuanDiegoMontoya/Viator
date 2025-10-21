import std.io;
import std.core;

struct ivec3
{
    s32 x, y, z;
} [[format("FormatIvec3")]];

struct pair
{
    ivec3 pos;
    u32 voxel;
} [[format("FormatPair")]];

struct String
{
    u64 sizeBytes;
    char data[sizeBytes];
} [[format("FormatString")]];

struct KeyValue
{
    u32 voxel;
    String name;
} [[format("FormatKeyValue")]];


struct NameDict
{
    u32 count;
    KeyValue pairs[count] [[inline]];
} [[format("FormatDict")]];

fn FormatIvec3(ref ivec3 v)
{
    return std::format("{}, {}, {}", v.x, v.y, v.z);
};

fn FormatString(ref String s)
{
    return std::format("\"{}\"", s.data);
};

fn FormatKeyValue(ref KeyValue kv)
{
    return std::format("{}: {}", kv.voxel, kv.name);
};

fn FormatDict(ref NameDict d)
{
    return std::format("count: {}", d.count);
};

fn FormatPair(ref pair p)
{
    return std::format("{}, {}, {}: {}", p.pos.x, p.pos.y, p.pos.z, p.voxel);
};

struct Thingy
{
    NameDict dict;
    u32 size;
    pair arr[size];
};

Thingy a @ 0 [[inline]];