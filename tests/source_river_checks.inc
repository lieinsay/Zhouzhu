void test_source_rivers() {
    std::printf("[source_rivers]\n");
    WorldGrid g; g.rows=g.cols=3; g.res=100; g.x0=g.z0=0;
    g.height.assign(9,100); g.shore.assign(9,1000); g.island.assign(9,0); g.landcover.assign(9,6); g.stream.assign(9,0);
    std::string error;
    const std::string source="rivers 1\nexponents .26 .4\nbasin 0 0 1 20\nbasin 1 .2 2 8\nthreshold .3 .3 10\n"
        "stem 0 -1 -1 0 0 1\npoint 50 0 100 98 20 2 13.16 1.05 30 1 25 40 2\npoint 50 200 100 98 20 2 13.16 1.05 30 1 25 40 2\n"
        "stem 1 0 0 -1 0 0\npoint 0 0 100 99.98 .02 .02 .01316 .0105 .3 .001 0 0 0\npoint 49 0 100 99.98 .02 .02 .01316 .0105 .3 .001 0 0 0\n";
    check(parse_source_rivers(source,g,error),"源河契约读取");
    WorldGrid bad=g;
    check(!parse_source_rivers(source+"stem 2 8 0 0 0 0\n",bad,error),"坏接点不静默补造");
    WorldTerrain terrain(g,{},42); const auto &net=terrain.get_rivers();
    check(net.from_source() && net.get_stems().size()==2,"直接读取源河段");
    const auto &tiny=net.get_stems()[1].v.front();
    check(std::abs(tiny.width-.02)<1e-12,"两厘米河槽不量化、不乘倍率");
    const auto &join=net.get_stems()[1].v.back(); const auto &down=net.get_stems()[0].v.front();
    check(join.x==down.x && join.z==down.z && join.bed()==down.bed(),"支流末点精确接源 join");
    check(!net.stage(down,0).wet && net.stage(down,1).wet,"日期推进刷新水情");
    check(net.stage(down,2).depth>down.depth && net.stage(down,2).half_width>down.half(),"涨水超过固定河槽");
    check(net.daily_index(tiny,1)==2 && !net.stage(tiny,2).wet,"rest 流域及溪涧断流");
    check(net.stage(down,4).depth==net.stage(down,1).depth,"日索引按源一年循环");
    TileLayout layout(g); const auto tile=build_tile_mesh(terrain,layout,layout.leaf_at(25,0));
    bool center=false;
    for(size_t i=0;i<tile.positions.size();i+=3) {
        const double x=tile.positions[i]+tile.origin_x,z=tile.positions[i+2]+tile.origin_z;
        if(std::abs(z)<1e-5 && x>0 && x<49 && tile.positions[i+1]<100) center=true;
    }
    check(center,"亚格河心插入实际地面三角形及碰撞");
    if(const char *dir=std::getenv("ZHOUZHU_RIVER_PACKAGE")) {
        std::string bytes,text; gen_core::read_file(std::string(dir)+"/terrain.bin",bytes); gen_core::read_file(std::string(dir)+"/rivers.tsv",text);
        WorldGrid actual;
        check(parse_world_grid(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),actual,error) && parse_source_rivers(text,actual,error),"实际6068包读取");
        WorldTerrain world(actual,{},42); const auto &r=world.get_rivers();
        size_t joins=0; double width=0; bool varies=false;
        for(size_t i=0;i<actual.source_stems.size();++i) {
            const auto &s=actual.source_stems[i]; const auto &v=r.get_stems()[i].v;
            for(size_t j=0;j<s.points.size();++j) { width=std::max(width,v[j].width); check(v[j].width==s.points[j].width,"实际宽度原值还原"); if(v[j].level>0) varies|=r.stage(v[j],10).depth!=r.stage(v[j],100).depth; }
            if(s.down>=0) { ++joins; const auto &p=r.get_stems()[size_t(s.down)].v[size_t(s.join)]; check(v.back().x==p.x && v.back().z==p.z && v.back().bed()==p.bed(),"6068接点连通"); }
            if(!v.empty() && v.front().level>0) varies|=r.stage(v.front(),10).depth!=r.stage(v.front(),100).depth;
        }
        check(joins>0 && width>20 && varies,"6068实包连接、宽度、不同日期");
        constexpr double spawn_x=24790,spawn_z=-32330;
        const auto spawn=world.sample(spawn_x,spawn_z);
        RiverHit hit; const bool river=r.nearest(spawn_x,spawn_z,1000,hit);
        check(spawn.land && river && hit.distance>hit.at.half()+5 && hit.distance<100,"出生在真实河岸地面");
        std::printf("6068: stems=%zu joins=%zu max_width=%.6f spawn=(%.1f,%.6f,%.1f) island=%d river_distance=%.6f channel=%.6f\n",r.get_stems().size(),joins,width,spawn_x,spawn.top,spawn_z,spawn.island,hit.distance,hit.at.width);
    }
}
