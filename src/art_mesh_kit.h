#pragma once
#include "mesh_kit.h"
#include <array>

// 纯数据的器物曲面，供游戏、独立模型导出和检查共用。绝不改变碰撞或世界生成数据。
namespace art_mesh_kit {
using namespace mesh_kit;
constexpr double TAU=6.283185307179586;
struct Profile {double r,y;};

inline V3 local_of(const Xform &xf,const V3 &point){const V3 q=point-xf.o;return{dot(q,{xf.m[0][0],xf.m[1][0],xf.m[2][0]}),dot(q,{xf.m[0][1],xf.m[1][1],xf.m[2][1]}),dot(q,{xf.m[0][2],xf.m[1][2],xf.m[2][2]})};}

inline void bevel_box(MeshPart &part,const Xform &xf,const V3 &size){
    const V3 half=size*.5;
    const double bevel=std::min(.018,.18*std::min({size.x,size.y,size.z}));
    if(bevel<.002){append_box(part,xf,size);return;}
    const V3 inner{half.x-bevel,half.y-bevel,half.z-bevel};
    const double dims[3]={size.x,size.y,size.z};const int grain=int(std::max_element(dims,dims+3)-dims);
    auto axis_half=[&](const V3 &axis){return std::abs(axis.x)*half.x+std::abs(axis.y)*half.y+std::abs(axis.z)*half.z;};
    auto rounded=[&](V3 q){V3 c{std::clamp(q.x,-inner.x,inner.x),std::clamp(q.y,-inner.y,inner.y),std::clamp(q.z,-inner.z,inner.z)};V3 d=q-c;const double length=std::sqrt(dot(d,d));return c+d*(bevel/std::max(1e-9,length));};
    for(int f=0;f<6;++f){
        const double hu=axis_half(FACE_U[f]),hw=axis_half(FACE_W[f]);
        const double us[]={-hu,-hu+bevel,hu-bevel,hu},ws[]={-hw,-hw+bevel,hw-bevel,hw};
        const int first=part.vertex_count();
        for(int i=0;i<3;++i)for(int j=0;j<3;++j){
            std::vector<V3> points;
            for(auto ij:{std::pair<int,int>{i,j},{i+1,j},{i+1,j+1},{i,j+1}})points.push_back(xf.xform(rounded(FACE_N[f]*axis_half(FACE_N[f])+FACE_U[f]*us[ij.first]+FACE_W[f]*ws[ij.second])));
            polygon(part,points,xf.rotate(FACE_N[f]),nullptr);
        }
        const int normal_axis=f/2,v_axis=grain==normal_axis?(normal_axis+1)%3:grain,u_axis=3-normal_axis-v_axis;
        const bool end=normal_axis==grain && std::max(dims[u_axis],dims[v_axis])/std::max(1e-6,std::min(dims[u_axis],dims[v_axis]))<3.5;
        for(int k=first;k<part.vertex_count();++k){
            const V3 q=local_of(xf,{part.positions[size_t(k)*3],part.positions[size_t(k)*3+1],part.positions[size_t(k)*3+2]});
            V3 n=q-V3{std::clamp(q.x,-inner.x,inner.x),std::clamp(q.y,-inner.y,inner.y),std::clamp(q.z,-inner.z,inner.z)};
            n=xf.rotate(n*(1.0/std::max(1e-9,std::sqrt(dot(n,n)))));
            part.normals[size_t(k)*3]=float(n.x);part.normals[size_t(k)*3+1]=float(n.y);part.normals[size_t(k)*3+2]=float(n.z);
            const double local[]={q.x,q.y,q.z};part.uvs[size_t(k)*2]=float(local[u_axis]+dims[u_axis]*.5);part.uvs[size_t(k)*2+1]=float(local[v_axis]+dims[v_axis]*.5);
            part.uv2s[size_t(k)*2]=end?float(-dims[u_axis]):0;part.uv2s[size_t(k)*2+1]=end?float(-dims[v_axis]):16;
        }
    }
}

inline void lathe(MeshPart &part,const Xform &xf,const V3 &size,const std::vector<Profile> &profile,int sides=32){
    double distance=0;
    for(size_t ring=1;ring<profile.size();++ring){
        const Profile a=profile[ring-1],b=profile[ring];const double dy=(b.y-a.y)*size.y,dr=b.r-a.r;
        const double span=std::hypot(dy,dr*.5*(size.x+size.z));
        if(span<1e-8)continue;
        auto point=[&](Profile p,double angle){return xf.xform({p.r*size.x*std::cos(angle),(p.y-.5)*size.y,p.r*size.z*std::sin(angle)});};
        auto normal=[&](double angle){V3 n{dy*std::cos(angle)/std::max(.001,size.x),-dr,dy*std::sin(angle)/std::max(.001,size.z)};return xf.rotate(n*(1.0/std::max(1e-9,std::sqrt(dot(n,n)))));};
        for(int side=0;side<sides;++side){
            const double aa=TAU*side/sides,ab=TAU*(side+1)/sides;const int first=part.vertex_count();
            polygon(part,{point(a,aa),point(a,ab),point(b,ab),point(b,aa)},normal((aa+ab)*.5),nullptr);
            for(int v=first;v<part.vertex_count();++v){
                const V3 q=local_of(xf,{part.positions[size_t(v)*3],part.positions[size_t(v)*3+1],part.positions[size_t(v)*3+2]});
                double angle=std::atan2(q.z/std::max(.001,size.z),q.x/std::max(.001,size.x));if(angle<aa-1e-6)angle+=TAU;
                const V3 n=normal(angle);part.normals[size_t(v)*3]=float(n.x);part.normals[size_t(v)*3+1]=float(n.y);part.normals[size_t(v)*3+2]=float(n.z);
                const double t=std::abs(dy)>1e-7?std::clamp((q.y-(a.y-.5)*size.y)/dy,0.0,1.0):0.5;
                part.uvs[size_t(v)*2]=float(angle*.5*(size.x+size.z));part.uvs[size_t(v)*2+1]=float(distance+t*span);
                if(std::abs(dy)<1e-8){part.uvs[size_t(v)*2]=float(q.x+size.x*.5);part.uvs[size_t(v)*2+1]=float(q.z+size.z*.5);part.uv2s[size_t(v)*2]=float(-size.x);part.uv2s[size_t(v)*2+1]=float(-size.z);}
            }
        }
        distance+=span;
    }
}

inline void ellipsoid(MeshPart &part,const Xform &xf,const V3 &size,bool sack=false){
    for(int row=0;row<12;++row)for(int side=0;side<24;++side){
        auto point=[&](int y,int a){double phi=3.141592653589793*y/12,theta=TAU*a/24;double r=.5*std::sin(phi);double yy=.5*std::cos(phi);
            if(sack){r*=1.0+.045*std::sin(theta*5+phi*2);r*=1.0-.6*std::pow(std::max(0.0,yy*2),6);if(yy<-.35)yy=-.5;}
            return xf.xform({r*size.x*std::cos(theta),yy*size.y,r*size.z*std::sin(theta)});};
        const int first=part.vertex_count();const double theta=TAU*(side+.5)/24,phi=3.141592653589793*(row+.5)/12;
        const V3 face=xf.rotate({std::sin(phi)*std::cos(theta),std::cos(phi),std::sin(phi)*std::sin(theta)});
        polygon(part,{point(row,side),point(row+1,side),point(row+1,side+1),point(row,side+1)},face,nullptr);
        for(int v=first;v<part.vertex_count();++v){
            const V3 q=local_of(xf,{part.positions[size_t(v)*3],part.positions[size_t(v)*3+1],part.positions[size_t(v)*3+2]});
            V3 n{q.x/std::max(.001,size.x*size.x),q.y/std::max(.001,size.y*size.y),q.z/std::max(.001,size.z*size.z)};n=xf.rotate(n*(1.0/std::max(1e-9,std::sqrt(dot(n,n)))));
            part.normals[size_t(v)*3]=float(n.x);part.normals[size_t(v)*3+1]=float(n.y);part.normals[size_t(v)*3+2]=float(n.z);
            double a=std::atan2(q.z/std::max(.001,size.z),q.x/std::max(.001,size.x));const double start=TAU*side/24;if(a<start-1e-6)a+=TAU;
            part.uvs[size_t(v)*2]=float(a*.5*(size.x+size.z));part.uvs[size_t(v)*2+1]=float(q.y+size.y*.5);
        }
    }
}

inline void tile_edge(MeshPart &mesh,const Xform &xf,const V3 &size){
    constexpr int slices=6;const double thickness=std::min(.012,size.y*.3);
    auto point=[&](int k,double z,bool inner){const double x=-.5+double(k)/slices;return xf.xform({x*size.x,(-.5+std::sqrt(std::max(0.0,1-4*x*x)))*size.y-(inner?thickness:0),z});};
    for(int k=0;k<slices;++k){
        const double x=-.5+(k+.5)/slices;const V3 n=xf.rotate({4*x*size.y/std::max(.001,size.x),std::sqrt(std::max(.0001,1-4*x*x)),0});
        polygon(mesh,{point(k,-size.z*.5,false),point(k,size.z*.5,false),point(k+1,size.z*.5,false),point(k+1,-size.z*.5,false)},n,nullptr);
        polygon(mesh,{point(k,-size.z*.5,true),point(k+1,-size.z*.5,true),point(k+1,size.z*.5,true),point(k,size.z*.5,true)},n*-1,nullptr);
        for(double z:{-size.z*.5,size.z*.5})polygon(mesh,{point(k,z,false),point(k+1,z,false),point(k+1,z,true),point(k,z,true)},xf.rotate({0,0,z<0?-1.0:1.0}),nullptr);
    }
}

inline void part(MeshPart &mesh,const BoxPart &p){
    const Xform xf(p.yaw,p.pitch,{p.x,p.y,p.z});const V3 size{p.sx,p.sy,p.sz};
    switch(p.shape){
        case VisualShape::BOX:append_box(mesh,xf,size);break;
        case VisualShape::BEVEL_BOX:bevel_box(mesh,xf,size);break;
        case VisualShape::VAT:lathe(mesh,xf,size,{{0,.02},{.30,0},{.43,.12},{.5,.45},{.44,.86},{.46,1},{.39,1},{.38,.86},{.41,.45},{.32,.13},{0,.13}});break;
        case VisualShape::BIN:lathe(mesh,xf,size,{{0,0},{.46,0},{.5,.05},{.5,.95},{.48,1},{.43,1},{.45,.12},{0,.12}});break;
        case VisualShape::SACK:ellipsoid(mesh,xf,size,true);break;
        case VisualShape::ELLIPSOID:ellipsoid(mesh,xf,size);break;
        case VisualShape::TILE_EDGE:tile_edge(mesh,xf,size);break;
        case VisualShape::HULL:{
            const double hx=size.x*.5,hy=size.y*.5,hz=size.z*.5;
            // 模块顶面/端面尺寸保持不动，舷侧至舭部以连续折弧收束，木纹沿船长。
            const std::vector<V3> profile={{-hx,hy,0},{hx,hy,0},{hx,.2*hy,0},{.97*hx,-.30*hy,0},{.86*hx,-.8*hy,0},{.70*hx,-hy,0},{-.70*hx,-hy,0},{-.86*hx,-.8*hy,0},{-.97*hx,-.30*hy,0},{-hx,.2*hy,0}};
            double distance=0;
            for(size_t edge=0;edge<profile.size();++edge){const V3 a=profile[edge],b=profile[(edge+1)%profile.size()],delta=b-a;const double span=std::hypot(delta.x,delta.y);const V3 normal=xf.rotate({-delta.y/span,delta.x/span,0});const int first=mesh.vertex_count();
                polygon(mesh,{xf.xform(a+V3{0,0,-hz}),xf.xform(b+V3{0,0,-hz}),xf.xform(b+V3{0,0,hz}),xf.xform(a+V3{0,0,hz})},normal,nullptr);
                for(int i=first;i<mesh.vertex_count();++i){const V3 v=local_of(xf,{mesh.positions[size_t(i)*3],mesh.positions[size_t(i)*3+1],mesh.positions[size_t(i)*3+2]});mesh.uvs[size_t(i)*2]=float(distance+dot(v-a,delta)/span);mesh.uvs[size_t(i)*2+1]=float(v.z+hz);}distance+=span;}
            for(double z:{-hz,hz}){std::vector<V3> points;for(auto p:profile)points.push_back(xf.xform(p+V3{0,0,z}));polygon(mesh,points,xf.rotate({0,0,z<0?-1.0:1.0}),nullptr);}
            break;}
        case VisualShape::DRUM:lathe(mesh,Xform(p.yaw,p.pitch+1.570796326794897,{p.x,p.y,p.z}),{p.sx,p.sz,p.sy},{{0,0},{.44,0},{.49,.18},{.5,.5},{.49,.82},{.44,1},{0,1}});break;
        case VisualShape::ROLLER_X:lathe(mesh,Xform(p.yaw+1.570796326794897,p.pitch+1.570796326794897,{p.x,p.y,p.z}),{p.sz,p.sx,p.sy},{{0,0},{.5,0},{.5,1},{0,1}},24);break;
        case VisualShape::DISC_Z:lathe(mesh,Xform(p.yaw,p.pitch+1.570796326794897,{p.x,p.y,p.z}),{p.sx,p.sz,p.sy},{{0,0},{.5,0},{.5,1},{0,1}});break;
        case VisualShape::RING_Y:case VisualShape::RING_Z:{std::vector<Profile> profile;for(int i=0;i<=12;++i){const double a=TAU*i/12;profile.push_back({.48+.02*std::cos(a),.5+.5*std::sin(a)});}const bool z=p.shape==VisualShape::RING_Z;lathe(mesh,Xform(p.yaw,p.pitch+(z?1.570796326794897:0),{p.x,p.y,p.z}),z?V3{p.sx,p.sz,p.sy}:size,profile);break;}
    }
}
}
