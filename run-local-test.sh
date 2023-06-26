#!/bin/bash
echo "0-Init"
cd `dirname $0`
rm node-1 node-2 node-3 build -rf || true

echo "1-Tar"
cd `dirname $0`
rm code.tar.gz -rf || true
#考生相关：指定这个code.tar.gz就是用于提交的代码压缩包
tar -czf code.tar.gz --directory=code .

echo "2-Build"
cd `dirname $0`
mkdir build
tar -xzf code.tar.gz --director=build
#考生相关: build.sh是构建入口脚本, run.sh是运行入口脚本
docker run --rm  -v "./build:/work" -w /work --network none public-images-registry.cn-hangzhou.cr.aliyuncs.com/public/alimama-2023:B-v0 bash build.sh
docker run --rm  -v "./testbench:/work" -w /work --network none public-images-registry.cn-hangzhou.cr.aliyuncs.com/public/alimama-2023:B-v0 bash build.sh

echo "3-Run"
cd `dirname $0`
cp build node-1 -rp
cp build node-2 -rp
cp build node-3 -rp
docker compose down
docker compose up -d
docker compose logs -f testbench
