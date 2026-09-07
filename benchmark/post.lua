-- wrk POST 测试脚本：向 /api/echo 发送 JSON body
wrk.method = "POST"
wrk.body   = [[{"msg":"hello","n":42}]]
wrk.headers["Content-Type"] = "application/json"
