// 从真实 indexed mesh 检查岛体连接和可见 T 缝，不把 raw sample 相等当作网格已经接上。
namespace terrain_seam_checks {
struct Point { double x, y, z; };
struct Line { double a, b, ya, yb; };
struct Skirt { std::array<double, 3> s, y; };
struct Edge { std::vector<Line> lines; std::vector<Skirt> skirts; };
Point point(const TileMesh &m, int k) { return { m.origin_x + m.positions[size_t(k)*3], m.positions[size_t(k)*3+1], m.origin_z + m.positions[size_t(k)*3+2] }; }
bool close(double a, double b) { return std::abs(a-b) < 2e-5; }
Edge edge(const TileMesh &m, bool along_z, double fixed) {
    Edge out;
    for(size_t k=0; k+2<m.indices.size(); k+=3) {
        const std::array<int,3> ids{m.indices[k],m.indices[k+1],m.indices[k+2]};
        const std::array<Point,3> p{point(m,ids[0]),point(m,ids[1]),point(m,ids[2])};
        if(!std::all_of(ids.begin(),ids.end(),[&](int v){return m.normals[size_t(v)*3+1]<0;})) continue;
        const double area=(p[1].x-p[0].x)*(p[2].z-p[0].z)-(p[1].z-p[0].z)*(p[2].x-p[0].x);
        auto on=[&](int i){return close(along_z?p[i].x:p[i].z,fixed);};
        auto s=[&](int i){return along_z?p[i].z:p[i].x;};
        if(area < -1e-8) for(int i=0;i<3;i++) {int j=(i+1)%3;if(on(i)&&on(j)&&std::abs(s(i)-s(j))>1e-6) out.lines.push_back({s(i),s(j),p[i].y,p[j].y});}
        if(std::abs(area)<1e-8 && on(0)&&on(1)&&on(2)) out.skirts.push_back({{s(0),s(1),s(2)},{p[0].y,p[1].y,p[2].y}});
    }
    return out;
}
bool height(const Edge &e,double s,double &y) {
    for(const auto &q:e.lines) if(s>=std::min(q.a,q.b)-1e-5 && s<=std::max(q.a,q.b)+1e-5) { y=q.ya+(q.yb-q.ya)*(s-q.a)/(q.b-q.a);return true; }
    return false;
}
std::vector<std::pair<double,double>> spans(const Edge &e,double s) {
    std::vector<std::pair<double,double>> out;
    for(const auto &q:e.skirts) {
        std::vector<double> ys;
        for(int i=0;i<3;i++) { int j=(i+1)%3;if(close(s,q.s[i])) ys.push_back(q.y[i]);
            if(std::abs(q.s[j]-q.s[i])>1e-8 && s>=std::min(q.s[i],q.s[j])-1e-8 && s<=std::max(q.s[i],q.s[j])+1e-8) ys.push_back(q.y[i]+(q.y[j]-q.y[i])*(s-q.s[i])/(q.s[j]-q.s[i])); }
        if(ys.size()>=2) out.emplace_back(*std::min_element(ys.begin(),ys.end()),*std::max_element(ys.begin(),ys.end()));
    }
    return out;
}
std::pair<size_t,double> gap(const Edge &a,const Edge &b,double start,double end) {
    std::vector<double> samples;for(const auto *e:{&a,&b}) for(const auto &q:e->lines) for(double s:{q.a,q.b,.5*(q.a+q.b)}) if(s>=start-1e-5&&s<=end+1e-5) samples.push_back(s);
    size_t count=0;double worst=0;
    for(double s:samples) {double ya,yb;if(!height(a,s,ya)||!height(b,s,yb)) continue;++count;
        const double lo=std::min(ya,yb),hi=std::max(ya,yb);auto intervals=spans(a,s);auto extra=spans(b,s);intervals.insert(intervals.end(),extra.begin(),extra.end());std::sort(intervals.begin(),intervals.end());
        double reach=lo,open=0;for(const auto &q:intervals) {if(q.second<lo||q.first>hi)continue;const double x=std::max(lo,q.first),z=std::min(hi,q.second);if(x>reach)open+=x-reach;reach=std::max(reach,z);}if(reach<hi)open+=hi-reach;worst=std::max(worst,open);
    }
    return {count,worst};
}
WorldGrid coast(double at) {
    WorldGrid g;g.rows=g.cols=17;g.res=100;g.x0=g.z0=-800;const size_t n=size_t(g.rows)*g.cols;
    g.height.assign(n,900);g.shore.resize(n);g.island.assign(n,0);g.landcover.assign(n,6);g.stream.assign(n,0);g.source_landscape=true;
    for(int r=0;r<g.rows;r++)for(int c=0;c<g.cols;c++)g.shore[g.index(r,c)]=float(at-.001*(g.x0+c*g.res)-(g.z0+r*g.res));return g;
}
}
void test_terrain_seams() {
    using namespace terrain_seam_checks;
    std::printf("[terrain_seams]\n");
    { // 6070 缓存中(-22200,11600)周围的原生4x4，岛0与22高差约500m。
        WorldGrid g;g.rows=g.cols=4;g.res=100;g.x0=-22350;g.z0=11450;g.source_landscape=true;
        g.height={844.786377f,307.125244f,310.378601f,313.815796f,844.786377f,815.890442f,309.159363f,311.846008f,842.739868f,815.890442f,815.890442f,310.922028f,818.661255f,826.850525f,826.850525f,310.922028f};
        g.shore={-92.396492f,-2.552974f,85.923874f,174.337372f,-27.185499f,-36.109322f,44.337929f,121.146881f,66.893814f,52.551647f,-18.156277f,52.607986f,165.772812f,76.161133f,-18.710047f,-21.481327f};
        g.island={22,0,0,0,22,22,0,0,22,22,22,0,22,22,22,0};g.landcover.resize(16);g.stream.assign(16,0);for(size_t k=0;k<16;k++)g.landcover[k]=g.shore[k]>0?1:0;
        WorldIsland low;low.id=0;low.rim_m=297.1;low.cliff_m=92.1;low.max_shore_m=13036.1;WorldIsland high;high.id=22;high.rim_m=844;high.cliff_m=100;high.max_shore_m=1000;
        WorldTerrain t(g,{low,high},42);TileLayout l(g);size_t top_cross=0,bottom_cross=0,top_faces=0,bottom_faces=0,wall_cross=0,low_walls=0,high_walls=0;
        for(const auto &key:l.select(-22200,11600)) {
            const auto m=build_tile_mesh(t,l,key);
            for(size_t q=0;q+2<m.indices.size();q+=3) {
                const std::array<int,3> ids{m.indices[q],m.indices[q+1],m.indices[q+2]};const std::array<Point,3> p{point(m,ids[0]),point(m,ids[1]),point(m,ids[2])};
                const double area=(p[1].x-p[0].x)*(p[2].z-p[0].z)-(p[1].z-p[0].z)*(p[2].x-p[0].x);
                const int a=g.island[g.nearest_cell(p[0].x,p[0].z)],b=g.island[g.nearest_cell(p[1].x,p[1].z)],c=g.island[g.nearest_cell(p[2].x,p[2].z)];
                if(std::all_of(ids.begin(),ids.end(),[&](int v){return std::abs(m.normals[size_t(v)*3+1])<1e-6;})) {
                    if(a!=b||a!=c)++wall_cross;else if(a==0)++low_walls;else if(a==22)++high_walls;
                }
                if(std::abs(area)<1e-8)continue;
                const bool top=std::all_of(ids.begin(),ids.end(),[&](int v){return m.normals[size_t(v)*3+1]>0;});
                const bool bottom=std::all_of(ids.begin(),ids.end(),[&](int v){return m.normals[size_t(v)*3+1]<0;});if(!top&&!bottom)continue;
                if(top) {++top_faces;if(a!=b||a!=c)++top_cross;}if(bottom){++bottom_faces;if(a!=b||a!=c)++bottom_cross;}
            }
        }
        std::printf("  source6070 native_window top_faces=%zu bottom_faces=%zu cross_island_top=%zu bottom=%zu\n",top_faces,bottom_faces,top_cross,bottom_cross);
        check(top_faces>0 && bottom_faces>0,"真实6070原生窗口经build_tile_mesh生成上下实体面");
        check(top_cross==0,"不同源岛的顶面不跨语义岛身份连接");check(bottom_cross==0,"不同源岛的底面不生成数百米尖长连接面");
        check(low_walls>0 && high_walls>0,"两座岛的岸缘和身份裁切均有闭合实体壁");check(wall_cross==0,"闭合壁只连接同一岛的顶与底，不以剔除面掩盖开放孔");
    }
    for(bool mixed:{false,true}) {
        auto g=coast(mixed?161.001:48.051);WorldIsland isl;isl.rim_m=800;isl.max_shore_m=1000;WorldTerrain t(g,{isl},42);TileLayout l(g);
        const auto a=build_tile_mesh(t,l,mixed?TileKey{3,3,3}:TileKey{4,6,6}),b=build_tile_mesh(t,l,mixed?TileKey{4,8,7}:TileKey{4,7,6});
        const double fixed=mixed?174:46,start=mixed?46:-82,end=mixed?174:46;const auto result=gap(edge(a,true,fixed),edge(b,true,fixed),start,end);
        std::printf("  coast seam=%s indexed_samples=%zu uncovered_m=%.9f\n",mixed?"mixed-lod":"stride4/1",result.first,result.second);
        check(result.first>20,"底部接缝核对真实索引边与竖向裙边覆盖");check(result.second<0.001,mixed?"不同LOD海岸底面不留可见T缝":"同LOD粗岛底与海岸细岛底不留T缝");
    }
}
