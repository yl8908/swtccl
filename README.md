# TCCL

## 目前此版本已支持socket，但进程数量仍必须为4的倍数, 每个进程只控制一个device（核组）
## 这个版本支持的数据类型是 int/uint/float/double/int64/uint64
## 支持的操作类型有allreduce/bcast

## 编译
```shell
$ cd tccl
$ make
```
TCCL 默认安装在 `tccl/build` 目录。

## tcclperftest使用方法（1.9.1a）：

直接运行tcclperftest默认是四核组。
tcclperftest n 就是运行 n 个核组。

```shell
$ # 四卡
$ tcclperftest 16
$ # 八卡
$ tcclperftest 32
```

注意： 不能指定卡，比如4卡机器只想测2张卡，tcclperftest 8 只测到0-7核组所在的前两张卡，默认前n个核组。

## 环境变量
TCCL_LOG_FLAG 打开运行时的日志，并配置日志保存位置
TCCL_SHARP_ENABLE 打开跨节点SHARP功能
TCCL_SHARP_NODE_NUM 配置跨节点SHARP支持的最小节点门槛数