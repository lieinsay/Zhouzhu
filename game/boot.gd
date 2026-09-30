extends Node
## 导出的游戏包的开局（project.godot 里 run/main_scene.template）：直接进剧情世界 seed42，
## 同 F2 选「世界 seed42」一条路（存档目录 user://worlds/seed42）。编辑器里跑照旧是 main.tscn。


func _ready() -> void:
	get_node("/root/DeveloperTools").switch_to.call_deferred("res://world_seed42.tscn")
