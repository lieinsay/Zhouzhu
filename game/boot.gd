extends Node
## Rider、编辑器及导出包使用同一开局入口，直接进剧情世界seed42。
## 同F2选「世界seed42」一条路，版本缓存目录由WorldSession::story_config统一决定。


func _ready() -> void:
	get_node("/root/DeveloperTools").switch_to.call_deferred("res://world_seed42.tscn")
