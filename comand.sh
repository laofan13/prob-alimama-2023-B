# help command
docker run --rm  -it  -v `pwd`:/work \                                                 
-w /work --env NODE_ID=1 --env NODE_NUM=2 --env MEMORY=4G --env CPU=2C \
public-images-registry.cn-hangzhou.cr.aliyuncs.com/public/alimama-2023:B-v1

## etcd
docker compose -f docker-compose-local.yml up

## work
docker run --rm  -it  -v ./code:/work -v ./data:/data:ro  -w /work \
--network alimama_2023_B_alimama public-images-registry.cn-hangzhou.cr.aliyuncs.com/public/alimama-2023:B-v1

## test
docker run --rm  -it  -v ./testbench:/work -v ./data:/data:ro  -w /work \
--network alimama_2023_B_alimama public-images-registry.cn-hangzhou.cr.aliyuncs.com/public/alimama-2023:B-v1