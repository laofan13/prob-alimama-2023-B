#!/bin/bash
echo "0-Init"
cd `dirname $0`
rm -rf node-1 node-2 node-3 build || true

echo "1-Tar"
cd `dirname $0`
rm -rf code.tar.gz || true
docker run --rm -v /`pwd`/://work -w //work --network none public-images-registry.cn-hangzhou.cr.aliyuncs.com/public/alimama-2023:B-v1 checktar code


echo "2-Build"
cd `dirname $0`
cp -rf code build
#考生相关: build.sh是构建入口脚本, run.sh是运行入口脚本
docker run --rm -v /`pwd`/build://work -w //work --network none public-images-registry.cn-hangzhou.cr.aliyuncs.com/public/alimama-2023:B-v1 bash build.sh
# docker run --rm -v /`pwd`/testbench://work -w //work --network none public-images-registry.cn-hangzhou.cr.aliyuncs.com/public/alimama-2023:B-v1 bash build.sh

echo "3-Run"
cd `dirname $0`
cp -rp build node-1
cp -rp build node-2
cp -rp build node-3 
docker compose down
docker compose up -d
# docker compose logs -f testbench
