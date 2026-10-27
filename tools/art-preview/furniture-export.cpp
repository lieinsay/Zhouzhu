// 直接复用 furniture_parts 和 mesh_kit，独立导出13种家具；不改变游戏几何。
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
#include "building_mesh.h"
#include "mesh_kit.h"
#include "art_mesh_kit.h"

template <typename T> void numbers(std::ostream &out, const std::vector<T> &values) {
    out << '[';
    for (size_t i=0;i<values.size();++i) { if(i) out << ','; out << values[i]; }
    out << ']';
}

int main(int argc,char **argv) {
    if(argc!=2) return 2;
    const auto directory=std::filesystem::u8path(argv[1]);
    std::filesystem::create_directories(directory);
    for(int i=0;i<furniture::COUNT;++i) {
        const auto &def=furniture::DEFS[i];
        Furnishing furnishing; furnishing.kind=def.id;
        std::vector<BoxPart> boxes;
        furniture_parts(furnishing,0,0,boxes);
        std::array<MeshPart,size_t(PartMaterial::COUNT)> parts;
        for(const auto &box:boxes) if(box.visual) {
            art_mesh_kit::part(parts[size_t(box.mat)],box);
        }
        std::ofstream out(directory/(std::string(def.id)+".json"),std::ios::binary);
        if(!out) return 3;
        out << std::setprecision(9) << "{\"schemaVersion\":1,\"units\":\"m\",\"winding\":\"ccw\",\"parts\":[";
        bool first=true;
        for(size_t mat=0;mat<parts.size();++mat) {
            const auto &p=parts[mat];if(p.empty())continue;
            if(!first)out<<',';first=false;
            float r,g,b,rough;part_material_color(PartMaterial(mat),r,g,b,rough);
            out << "{\"positions\":";numbers(out,p.positions);
            out << ",\"normals\":";numbers(out,p.normals);
            out << ",\"uvs\":";numbers(out,p.uvs);
            out << ",\"uv2s\":";numbers(out,p.uv2s);
            out << ",\"indices\":[";
            for(size_t k=0;k<p.indices.size();k+=3) {
                if(k)out<<',';out<<p.indices[k]<<','<<p.indices[k+2]<<','<<p.indices[k+1];
            }
            out << "],\"color\":["<<r<<','<<g<<','<<b<<"],\"roughness\":"<<rough<<",\"materialIndex\":"<<mat<<'}';
        }
        out << "]}\n";
        if(first || !out) return 4;
    }
    std::cout << furniture::COUNT << " furniture meshes exported\n";
    return 0;
}
