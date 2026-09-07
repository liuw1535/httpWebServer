# 压测说明

对 CppExpress 进行基准测试，量化高并发下的吞吐与延迟。

## 准备

1. 构建 Release 版本并启动示例服务器：

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 启动（另开一个终端），示例程序监听 3000
./example
```

2. 安装压测工具，任选其一：

```bash
# wrk（推荐，现代多线程）
sudo apt install wrk        # 或: brew install wrk

# 或 webbench（经典）
sudo apt install webbench
```

## 用 wrk 压测

```bash
# 轻载：4 线程 × 1000 连接，30 秒
wrk -t4 -c1000 -d30s --latency http://127.0.0.1:3000/

# 中载：8 线程 × 5000 连接，30 秒（Keep-Alive 长连接）
wrk -t8 -c5000 -d30s --latency http://127.0.0.1:3000/api/hello

# 短连接场景（关闭 Keep-Alive）
wrk -t4 -c1000 -d30s -H "Connection: close" --latency http://127.0.0.1:3000/

# 带路径参数（测试路由提取性能）
wrk -t4 -c1000 -d30s --latency http://127.0.0.1:3000/api/users/42
```

脚本化版本见 `benchmark/bench.sh`。

## 用 webbench 压测

```bash
# 1000 客户端，30 秒
webbench -c 1000 -t 30 http://127.0.0.1:3000/
```

## 记录结果

压测后请将结果填入下方表格，并在主 README 的「性能」章节引用。

| 场景 | 并发连接 | QPS | 平均延迟 | P99 延迟 | 备注 |
|------|----------|-----|----------|----------|------|
| Hello（Keep-Alive） | 1000 | _待填_ | _待填_ | _待填_ | |
| Hello（Keep-Alive） | 5000 | _待填_ | _待填_ | _待填_ | |
| 短连接 | 1000 | _待填_ | _待填_ | _待填_ | `Connection: close` |
| 路径参数 :id | 1000 | _待填_ | _待填_ | _待填_ | |

> 测试环境：CPU _待填_、内存 _待填_、OS _待填_、GCC _待填_，Release `-O2`。
