# MAX-Code首届阿里妈妈极限代码挑战赛 - 复赛

MAX-Code首届阿里妈妈极限代码挑战赛，以“超越极限，不断成长”为宗旨，由阿里妈妈技术团队发起。

大赛依托阿里妈妈真实的广告业务场景，聚焦实际工程问题，分为AI赛道和引擎赛道。
AI赛道聚焦机器学习推理服务的性能优化
引擎赛道聚焦真实场景的广告检索引擎构建

## 比赛规则
1. 赛题简述：广告检索引擎是在线广告系统的核心模块，涉及到广告的匹配、过滤、排序、计价等核心能力。在单机无法容纳的广告数据规模下，如何实现高性能的广告检索是持续探索的技术问题。本题将提供一份广告数据集，要求设计实现一个包含核心功能的高性能的分布式广告检索引擎；
2. 提交方式：复赛阶段采用源代码打包上传的提交方式，压缩包目录和文件规范请在复赛开放后参考题目相关说明；
3. 复赛评测说明：「引擎赛道」系统每天为每个团队提供10次正式评测的机会，复赛提交截止时间：2023年7月23日20:00；
3=4. 排行榜更新：在复赛期间，我们会根据比赛前2周参赛者提交运行的情况，可能会进行一些判分限制的调整，这段期间内不会公开展示排行榜，正式的排行榜将在2023年7月10日22:00起展示，之前的正式提交的程序会进行重新跑分，按照赛题评测指标从高到低排序。排行榜将基于参赛团队在本阶段历史最优成绩进行排名展示，最终结果以晋级榜单为准；
1. 晋级榜单：复赛结束，以排行榜成绩作为复赛成绩参考依据，组委会将会对参与队伍的代码进行人工审核。如果存在相互作弊、未按赛题要求实现等行为，组委会有权取消其参赛资格，晋级空缺名额递补，最终复赛晋级榜单将于2023年7月24日公布。

## 赛题说明
构建分布式mini广告检索引擎
广告业务由广告主、媒体、广告平台等几个角色组成。假设一个典型的电商搜索广告场景：广告主在广告平台中创建了若干推广计划（带有投放时段、投放状态等属性），每个计划下可以创建若干推广单元；每个推广单元可以绑定一个商品（一个商品可以绑定在多个推广单元上），并为该商品选择若干推广关键词、设置该关键词的出价。如下图所示：

![url_to_image](https://uploadfiles.nowcoder.com/images/20230625/0_1687694798402/F9B74D31491659546B143BA388266646)

当消费者在电商APP中搜索某个关键词的时候，会触发一个广告请求。广告平台中的广告引擎，会根据当前请求中的关键词（可能有多个）、时段等信息，召回关键词匹配、时段匹配、投放状态匹配的广告（注1），并对召回的多个广告进行排序、计算计费价格（注2）。

请设计实现一个包含上述功能的广告引擎，加载【数据文件】中的广告数据（注3），响应并发的广告请求，为每个请求返回排名TopN的推广单元及对应的计费价格。

运行时环境要求，详见（注4）。评分规则，详见该说明最后的描述，其他细节描述见(注5)，接口描述见(注6)。

### 说明：
注1：
广告召回规则说明。例如有如下4组推广计划-推广单元：
+ 计划1（投放时段为0,1,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,1,0,0,0，投放状态为1），单元1（商品1，选择了关键词1,关键词2）
+ 计划2（投放时段为0,1,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,1,0,0,0，投放状态为1），单元2（商品2，选择了关键词2,关键词3）
+ 计划3（投放时段为0,1,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,1,0,1,0,0,0，投放状态为1），单元3（商品3，选择了关键词1,关键词3）
+ 计划4（投放时段为0,1,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,1,0,0,0，投放状态为0），单元4（商品4，选择了关键词1,关键词4）
对于"关键词1、时段10"的一个请求，只有单元1可以被召回（单元2关键词不匹配，单元3投放时段不匹配，单元4投放状态不匹配）。

注2：
广告排序和计费规则说明。
+ 排序分数 = 预估点击率 x 出价（分数越高，排序越靠前）
+ 预估点击率 = 商品向量 和 用户_关键词向量 的余弦距离
+ 计费价格 = 第 i+1 名的排序分数 / 第 i 名的预估点击率（i表示排序名次，例如i=1代表排名第1的广告）

注3：
广告数据格式说明。文件包含多列，各列之间用 Tab符 分隔。
+ 【数据文件】评测环境文件路径为: /data/raw_data.csv ，字段说明：
  + keywrod: 关键词
  + adgroup_id: 广告单元id
  + keyword_prices: 出价列表
  + status: 投放状态（取值范围为[0, 1]，1代表可投）
  + timings：投放时段（逗号分隔的24个值，取值范围为[0, 1]，1代表可投）
  + vector：商品向量（float类型的2维向量）
  + campaign_id：计划id
  + item_id：商品id
+ 【数据文件】格式示例如下，提供的数据为csv文件不含 header：

keywrod(uint64)|adgroup_id(uint64)|status(int8)|price(uint16)|timings(int8, 0/1列表，长度24)|vector(float列表)|campaign_id(uint64)|item_id(uint64)
  1. 4803367238	1797465973516	17433	1	0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0	0.130628,0.991431	80576767129     1796338105989
  2. 4803367238	1746215892460	19349	1	0,1,0,0,0,0,1,0,1,0,0,0,0,0,0,0,1,1,0,0,1,0,0,0	0.837057,0.547116	28567131740	    1744431436612
  3. 4803367238	745467736338	42862	1	0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,1,1,0,0,0,0,0	0.558704,0.829367	314634501119	744329902992
  4. 4803367238	189661544708	53515	1	0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0	0.649985,0.759947	189162776031	190443671389
  5. 4803367238	1881171892257	7993	1	0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0	0.260188,0.965558	161844176350	1877791750111
  6. 4803367238	722542970812	42351	1	0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0,0	0.659703,0.751526	295630862208	723587381485
  7. 4803367238	76707708741	    23025	1	0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0	0.377738,0.925913	75543429811	    75905118724

request: 4803367238	0.552321,0.833632	20	1

1. match result
  1. 4803367238	1746215892460	19349	1	0,1,0,0,0,0,1,0,1,0,0,0,0,0,0,0,1,1,0,0,1,0,0,0	0.837057, 0.547116	28567131740	1744431436612
  2. 4803367238	722542970812	42351	1	0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0,0	0.659703, 0.751526	29563086220 723587381485

1. score evaluate
    预估点击率 = 商品向量 和 用户_关键词向量 的余弦距离
    排序分数 = 预估点击率 x 出价（分数越高，排序越靠前）

    1. 1746215892460:
        [0.837057, 0.547116] [0.552321,0.833632]
        19349
        预估点击率 = （0.837057 * 0.552321 + 0.547116 * 0.833632）/ (sqrt(0.837057^2 + 0.547116^2) + sqrt(0.552321^2 + 0.833632^2))
                = 0.9184170424
        排序分数 = 17770.45135

    2. 722542970812:
        [0.659703, 0.751526] [0.552321,0.833632]
        42351
        预估点击率 = （0.659703 * 0.552321 + 0.751526 * 0.833632）/ (sqrt(0.659703^2 + 0.751526^2) + sqrt(0.552321^2 + 0.833632^2))
                = 0.9908638562
        排序分数 = 41964.07517

3. top_k：
   1. 722542970812, 20179.83617
   2. 1746215892460, 17770.45135

4. 计费价格（计费价格 = 第 i+1 名的排序分数 / 第 i 名的预估点击率（i表示排序名次，例如i=1代表排名第1的广告））
   1. 17770.45135 / 0.9908638562 = 17934.30171

5. result:
  722542970812	17934
