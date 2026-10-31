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
    // v2（复核稿 8.3）：中心线位置 cx cz 与点间弯点；弯点各量按弧长内插，支流按源点下标接到干流的锚点而不是弯点
    {
        const std::string head="rivers 2\nexponents .26 .4\nbasin 0 1\nthreshold .3\n";
        const std::string v2=head+
            "stem 0 -1 -1 0 0 1\n"
            "point 50 0 100 98 20 2 13 1 30 1 25 40 2.0 52 0\n"   // 级别照源写成 2.0
            "bend 54 50\nbend 54 100\nbend 54 150\n"
            "point 50 200 100 96 30 2 13 1 30 1 25 40 2.0 52 200\n"
            "stem 1 0 1 -1 0 0\n"
            "point 0 300 100 99 .02 .02 .01 .01 .3 .001 0 0 0 0 300\n"
            "bend 30 250\n";
        WorldGrid curved=g;
        check(parse_source_rivers(v2,curved,error),"v2 中心线与弯点读取");
        WorldTerrain bent(curved,{},42); const auto &cs=bent.get_rivers().get_stems();
        const auto &main=cs[0].v;
        check(main.size()==5 && main[0].x==52 && main[1].x==54 && main[1].z==50 && main[4].x==52 && main[4].z==200,"顶点 = 锚点中心线位置 + 弯点");
        check(std::abs(main[2].width-25)<1e-9 && std::abs(main[2].surface-99)<1e-9 && main[2].s>main[1].s && main[4].s>main[3].s,
            "弯点各量按弧长在两锚点间内插");
        check(cs[1].v.size()==3 && cs[1].v.back().x==52 && cs[1].v.back().z==200,"支流接干流第 1 个源点（锚点），不是第 1 个顶点");
        WorldGrid reject=g;
        check(!parse_source_rivers("rivers 1\nexponents .26 .4\nbasin 0 1\nthreshold .3\nstem 0 -1 -1 0 0 1\n"
            "point 50 0 100 98 20 2 13 1 30 1 25 40 2\nbend 54 50\npoint 50 200 100 96 30 2 13 1 30 1 25 40 2\n",reject,error),"v1 不认弯点");
        check(!parse_source_rivers(head+"stem 0 -1 -1 0 0 1\npoint 50 0 100 98 20 2 13 1 30 1 25 40 2 52 0\n"
            "point 50 200 100 96 30 2 13 1 30 1 25 40 2 52 200\nbend 60 260\n",reject,error),"没有汇入点的段末不许有弯点");
        // 同一条直河：100 m 一段与插了弯点的 25 m 一段，横断面上的地面应当一样（合成不能随分段疏密变）
        WorldGrid sparse=g, dense=g;
        sparse.height.assign(9,110); dense.height.assign(9,110);
        std::string thin=head+"stem 0 -1 -1 0 0 1\n", fine=head+"stem 0 -1 -1 0 0 1\n";
        for(int k=-3;k<=3;++k) {
            const double z=100.0*k, sf=(k+3)/6.0;
            char line[160];
            std::snprintf(line,sizeof line,"point 0 %g %g %g 16 1 10 .6 80 5 60 60 2.0 0 %g\n",z,108-2*sf,105-2*sf,z);
            thin+=line; fine+=line;
            if(k<3) for(int q=1;q<4;++q) { std::snprintf(line,sizeof line,"bend 0 %g\n",z+25.0*q); fine+=line; }
        }
        check(parse_source_rivers(thin,sparse,error) && parse_source_rivers(fine,dense,error),"直河疏密两版读取");
        WorldTerrain ts(sparse,{},42), td(dense,{},42);
        double worst=0;
        for(double x=0;x<=90;x+=3) worst=std::max(worst,std::abs(ts.sample_raw(x,40).top-td.sample_raw(x,40).top));
        std::printf("  straight river sparse vs dense: worst %.4f m\n",worst);
        check(worst<.01,"同一条直河 100 m 与 25 m 分段的横断面一致");
    }
    TileLayout layout(g); const auto tile=build_tile_mesh(terrain,layout,layout.leaf_at(25,0));
    bool center=false;
    for(size_t i=0;i<tile.positions.size();i+=3) {
        const double x=tile.positions[i]+tile.origin_x,z=tile.positions[i+2]+tile.origin_z;
        if(std::abs(z)<1e-5 && x>0 && x<49 && tile.positions[i+1]<100) center=true;
    }
    check(center,"亚格河心插入实际地面三角形及碰撞");
    // 同一条河较远的宽谷已覆盖此处，最近窄谷的零权重候选刚进范围时不能把它突然替掉。
    WorldGrid overlap=g;
    overlap.height.assign(9,200); overlap.source_landscape=true;
    const std::string overlap_source="rivers 1\nexponents .26 .4\nbasin 0 1\nthreshold .3\n"
        "stem 0 -1 -1 0 0 1\n"
        "point 0 0 100 98 20 2 13 1 30 1 0 0 2\n"
        "point 0 100 100 98 20 2 13 1 30 1 0 0 2\n"
        "point 0 200 100 98 20 2 13 1 30 1 200 200 2\n"
        "point 0 300 100 98 20 2 13 1 30 1 200 200 2\n";
    check(parse_source_rivers(overlap_source,overlap,error),"重叠宽窄谷契约读取");
    WorldTerrain valley(overlap,{},42);
    const auto before=valley.sample_raw(39.999,50), after=valley.sample_raw(40.001,50);
    std::printf("valley entry: %.6f -> %.6f (delta %.6f m / .002 m)\n",before.top,after.top,after.top-before.top);
    check(std::abs(before.top-after.top)<.01,"零权重近河段进入范围时地面连续");
    check(std::abs(valley.sample_raw(0,50).top-98)<1e-9,"重叠谷保留源河心河床");
    WorldGrid endpoint=overlap;
    check(parse_source_rivers("rivers 1\nexponents .26 .4\nbasin 0 1\nthreshold .3\n"
        "stem 0 -1 -1 0 0 1\n"
        "point 0 0 100 98 20 2 13 1 30 1 0 200 2\n"
        "point 0 100 100 98 20 2 13 1 30 1 0 200 2\n",endpoint,error),"不对称谷宽端点读取");
    WorldTerrain cap(endpoint,{},42);
    check(std::abs(cap.sample_raw(-.001,-50).top-cap.sample_raw(.001,-50).top)<.01,"端点左右谷宽切换连续");
    const auto edge=cap.sample_raw(500,50);
    check(std::abs(edge.top-(endpoint.height_at(500,50)+cap.detail(500,50)))<1e-9 && edge.bank==0,"影响范围外仍取源地形和细节");
    check(std::abs(cap.sample_raw(-10,50).top-100)<1e-9 && std::abs(cap.sample_raw(10,50).top-100)<1e-9,
        "单段河槽真实半宽处保留源平岸高程，不因左右谷宽不同而放宽");
    const double half_bed=98+2*std::pow(.5,.4/.26);
    check(std::abs(cap.sample_raw(-5,50).top-half_bed)<1e-9 && std::abs(cap.sample_raw(5,50).top-half_bed)<1e-9,
        "单段河槽两岸内侧仍按源宽深和横断面取值");
    WorldGrid bend=overlap;
    check(parse_source_rivers("rivers 1\nexponents .26 .4\nbasin 0 1\nthreshold .3\n"
        "stem 0 -1 -1 0 0 1\n"
        "point 0 0 100 98 20 2 13 1 30 1 20 20 2\n"
        "point 0 100 95 93 20 2 13 1 30 1 20 20 2\n"
        "point 100 100 90 88 20 2 13 1 30 1 20 20 2\n",bend,error),"不同高程折弯源河读取");
    WorldTerrain turn(bend,{},42);
    const double bend_before=turn.sample_raw(29.999,70).top, bend_after=turn.sample_raw(30.001,70).top;
    std::printf("bend bisector: %.6f -> %.6f (delta %.6f m / .002 m)\n",bend_before,bend_after,bend_after-bend_before);
    check(std::abs(bend_before-bend_after)<.01,"弯道相邻河段等距线两侧高度连续，不因最近段切换突跳");
    check(std::abs(turn.sample_raw(0,100).top-93)<1e-9,"弯道共享源点保留真实河床原值");
    check(std::abs(turn.sample_raw(63.999,80).top-turn.sample_raw(64.001,80).top)<.01,
        "跨空间桶边界时零影响候选增减不造成地面突跳");
    // 冻结分享截图处的旧源断面，避免生成器修正谷宽后，真实旧候选切换病例随正式包一起消失。
    WorldGrid wall=overlap; wall.height.assign(9,1231.11887335254);
    check(parse_source_rivers("rivers 1\nexponents .26 .4\nbasin 0 1\nthreshold .3\n"
        "stem 0 -1 -1 0 0 1\n"
        "point 24550 -32250 1225.2169189453125 1224.5162353515625 19.031383514404297 .700677216053009 12.523859024047852 .3680696189403534 115.20999908447266 4.565074443817139 61 61 2\n"
        "point 24450 -32350 1213.926025390625 1213.1014404296875 20.11415672302246 .8246574997901917 13.236392974853516 .43319717049598694 115.25 4.565958023071289 61 146 2\n"
        "point 24350 -32450 1213.136474609375 1212.4154052734375 20.100780487060547 .721089243888855 13.227590560913086 .3787921965122223 116.62999725341797 4.5989990234375 61 145 2\n",wall,error),"旧6068断墙源断面读取");
    WorldTerrain frozen_wall(wall,{},42);
    auto old_segment=[&](double x) {
        RiverHit hits[WorldRivers::MAX_HITS];
        const int count=frozen_wall.get_rivers().near(x,-32356,hits,WorldRivers::MAX_HITS);
        return count ? hits[0].segment : -1;
    };
    double lo=24612,hi=24614;
    const int segment=old_segment(lo);
    check(segment>=0 && segment!=old_segment(hi),"冻结断面保留旧最近河段切换触发条件");
    for(int k=0;k<40;++k) {const double mid=(lo+hi)*.5;if(old_segment(mid)==segment)lo=mid;else hi=mid;}
    const double boundary=(lo+hi)*.5;
    const double frozen_jump=frozen_wall.sample_raw(boundary+1e-5,-32356).top-frozen_wall.sample_raw(boundary-1e-5,-32356).top;
    std::printf("frozen 6068 switch x=%.9f new_jump=%.12f m / .00002 m\n",boundary,frozen_jump);
    check(std::abs(frozen_jump)<.001,"冻结真实宽窄谷断面微跨界没有高度阶跃");
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
        // 分享截图旁旧逻辑在这一对实包坐标：源地形 +0.079 m，游戏地面 -14.119 m。
        const auto wall_a=world.sample_raw(24612,-32356), wall_b=world.sample_raw(24614,-32356);
        std::printf("6068 wall: source_delta=%.9f terrain_delta=%.9f\n",actual.height_at(24614,-32356)-actual.height_at(24612,-32356),wall_b.top-wall_a.top);
        const double jump=world.sample_raw(boundary+1e-5,-32356).top-world.sample_raw(boundary-1e-5,-32356).top;
        std::printf("6068 old switch x=%.9f new_jump=%.12f m / .00002 m\n",boundary,jump);
        check(std::abs(jump)<.001,"6068实际候选切换点没有高度阶跃，保留连续谷坡");
        const auto spawn=world.sample(spawn_x,spawn_z);
        RiverHit hit; const bool river=r.nearest(spawn_x,spawn_z,1000,hit);
        check(spawn.land && river && hit.distance>hit.at.half()+5 && hit.distance<100,"出生在真实河岸地面");
        std::printf("6068: stems=%zu joins=%zu max_width=%.6f spawn=(%.1f,%.6f,%.1f) island=%d river_distance=%.6f channel=%.6f\n",r.get_stems().size(),joins,width,spawn_x,spawn.top,spawn_z,spawn.island,hit.distance,hit.at.width);
    }
}
