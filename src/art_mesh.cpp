#include "art_mesh.h"
#include "mesh_kit.h"
#include "art_mesh_kit.h"
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
using namespace godot;
Ref<ArrayMesh> art_mesh::from_part(const MeshPart &p_part,const Ref<Material> &p_material) {
    Ref<ArrayMesh> mesh;mesh.instantiate();if(p_part.empty())return mesh;
    PackedVector3Array vertices,normals;PackedInt32Array indices;
    vertices.resize(p_part.vertex_count());normals.resize(p_part.vertex_count());indices.resize(int64_t(p_part.indices.size()));
    for(int i=0;i<p_part.vertex_count();++i){vertices.set(i,Vector3(p_part.positions[size_t(i)*3],p_part.positions[size_t(i)*3+1],p_part.positions[size_t(i)*3+2]));normals.set(i,Vector3(p_part.normals[size_t(i)*3],p_part.normals[size_t(i)*3+1],p_part.normals[size_t(i)*3+2]));}
    std::copy(p_part.indices.begin(),p_part.indices.end(),indices.ptrw());
    Array arrays;arrays.resize(Mesh::ARRAY_MAX);arrays[Mesh::ARRAY_VERTEX]=vertices;arrays[Mesh::ARRAY_NORMAL]=normals;arrays[Mesh::ARRAY_INDEX]=indices;
    if(p_part.uvs.size()==size_t(p_part.vertex_count())*2){
        PackedVector2Array uv,uv2;uv.resize(p_part.vertex_count());uv2.resize(p_part.vertex_count());
        for(int i=0;i<p_part.vertex_count();++i){uv.set(i,Vector2(p_part.uvs[size_t(i)*2],p_part.uvs[size_t(i)*2+1]));uv2.set(i,Vector2(p_part.uv2s[size_t(i)*2],p_part.uv2s[size_t(i)*2+1]));}
        arrays[Mesh::ARRAY_TEX_UV]=uv;arrays[Mesh::ARRAY_TEX_UV2]=uv2;
    }
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES,arrays);mesh->surface_set_material(0,p_material);return mesh;
}
Ref<ArrayMesh> art_mesh::box(const Vector3 &p_size,const Ref<Material> &p_material){MeshPart part;mesh_kit::append_box(part,mesh_kit::Xform(0,0,{}),{p_size.x,p_size.y,p_size.z});return from_part(part,p_material);}
Ref<ArrayMesh> art_mesh::shape(const BoxPart &p_part,const Ref<Material> &p_material){MeshPart part;art_mesh_kit::part(part,p_part);return from_part(part,p_material);}
