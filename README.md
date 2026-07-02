#  覆盖式差异备份

包含：`repo status` 中实际新增/修改的源码、配置、下载包、固件包；未追踪目录完整展开复制。


使用：
```sh
scripts/apply_overlay.sh /path/to/TinaSDK
# 审查 meta/delete_list.txt 后，如需删除：
scripts/apply_deletes.sh /path/to/TinaSDK
```
