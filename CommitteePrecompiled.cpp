
#include "CommitteePrecompiled.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libethcore/ABI.h>
#include <libprecompiled/TableFactoryPrecompiled.h>
#include <typeinfo>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

// data struct 无序map
typedef std::unordered_map<std::string, std::string> Pair;
typedef std::unordered_map<std::string, float> Scores;

/*
contract CommitteePrecompiled{
    function RegisterNode() public;                                 //节点注册
    function QueryState() public view returns(string, int);         //查询状态
    function QueryGlobalModel() public view returns(string, int);   //获取全局模型
    function UploadLocalUpdate(string update, int256 epoch) public; //上传本地模型
    function UploadScores(int256 epoch, string scores) public;      //上传评分
    function QueryAllUpdates() public view returns(string);         //获取所有本地模型
}
*/

// table name 
const std::string TABLE_NAME = "CommitteePrecompiled";
// key field
const std::string KEY_FIELD = "key";
const std::string EPOCH_FIELD_NAME = "epoch";                        //当前迭代轮次(int)-->触发链下训练
// const std::string NODE_COUNT_FIELD_NAME = "node_count";           //注册的客户端数量(size_t)-->触发链上角色初始化及FL开始(epoch=0)
const std::string UPDATE_COUNT_FIELD_NAME = "update_count";          //当前已上传本地更新的数量(size_t)-->触发链下评分
const std::string SCORE_COUNT_FIELD_NAME = "score_count";            //当前已上传评分数量(size_t)-->触发链上聚合
const std::string ROLES_FIELD_NAME = "roles";                        //保存各个客户端的角色(unordered_map<Adress,string>)
const std::string LOCAL_UPDATES_FIELD_NAME = "local_updates";        //保存所有已上传的模型更新(unordered_map<Adress,string>)
const std::string LOCAL_SCORES_FIELD_NAME = "local_scores";          //保存所有已上传的模型评分(unordered_map<Adress,string>, string=unordered<Adress,float>)
const std::string GLOBAL_MODEL_FIELD_NAME = "global_model";          //保存全局模型(string)
// value field
const std::string VALUE_FIELD = "value";

//interface name 定义类中所有的接口
const char* const REGISTER_NODE = "RegisterNode()";                         //节点注册
const char* const QUERY_STATE = "QueryState()";                             //查询状态
const char* const QUERY_GLOBAL_MODEL = "QueryGlobalModel()";                //获取全局模型
const char* const UPLOAD_LOCAL_UPDATE = "UploadLocalUpdate(string,int256)"; //上传本地模型
const char* const UPLOAD_SCORES = "UploadScores(int256,string)";            //上传评分
const char* const QUERY_ALL_UPDATES = "QueryAllUpdates()";                  //获取所有本地模型

//将json对象转为字符串格式
template<class T>
std::string to_json_string(T& t) {
    json j = t;
    // j.dump()返回至前端可被解析
    return j.dump();
}


//排序
int partition(std::vector<float>& scores, int left, int right)
{
    int pos = right;
    right--;
    while (left <= right)
    {
        while (left < pos && scores[left] <= scores[pos])
            left++;

        while (right >= 0 && scores[right] > scores[pos])
            right--;

        if (left >= right)
            break;

        std::swap(scores[left], scores[right]);
    }
    std::swap(scores[left], scores[pos]);
    return left;
}


//中间分数
float GetMid(std::vector<float>& scores) {
    int left = 0;
    int right = scores.size() - 1;
    //右移一位，除以二，中间位置
    int midPos = right >> 1;
    int index = 0;

    while (index != midPos)
    {
        index = partition(scores, left, right);

        if (index < midPos)
        {
            left = index + 1;
        }
        else if (index > midPos)
        {
            right = index - 1;
        }
    }
    assert(index == midPos);

    float midValue;
    if (right % 2 != 0) {
        float t = scores[index + 1];
        // loops can be expanded to optimize
        for (int i = index + 2; i <= right; i++)
            if (scores[i] < t)
                t = scores[i];
        midValue = (scores[index] + t) / 2;
    }
    else
        midValue = scores[index];

    return midValue;
}

// compare the struct by value
bool cmp_by_value(const std::pair<std::string, float>& it1, const std::pair<std::string, float>& it2) {
    return it1.second > it2.second;
}


// 在构造函数进行接口注册
CommitteePrecompiled::CommitteePrecompiled()                              //构造，初始化函数选择器对应的函数
{
    // name2Selector是基类Precompiled类中成员，保存接口调用的映射关系
    name2Selector[REGISTER_NODE] = getFuncSelector(REGISTER_NODE);
    name2Selector[QUERY_STATE] = getFuncSelector(QUERY_STATE);
    name2Selector[QUERY_GLOBAL_MODEL] = getFuncSelector(QUERY_GLOBAL_MODEL);
    name2Selector[UPLOAD_LOCAL_UPDATE] = getFuncSelector(UPLOAD_LOCAL_UPDATE);
    name2Selector[UPLOAD_SCORES] = getFuncSelector(UPLOAD_SCORES);
    name2Selector[QUERY_ALL_UPDATES] = getFuncSelector(QUERY_ALL_UPDATES);
}

PrecompiledExecResult::Ptr CommitteePrecompiled::call(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _param,
    Address const& _origin, Address const&)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CommitteePrecompiled") << LOG_DESC("call")
        << LOG_KV("param", toHex(_param));

    // parse function name
    uint32_t func = getParamFunc(_param);                                           //六位函数选择器
    bytesConstRef data = getParamData(_param);                                      //参数
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();    //声明结果
    callResult->gasPricer()->setMemUsed(_param.size());                             //分配内存
    dev::eth::ContractABI abi;                                                      //声明abi

    // the hash as a user-readable hex string with 0x perfix
    std::string _origin_str = _origin.hexPrefixed();

    // open table if table is exist
    Table::Ptr table = openTable(_context, precompiled::getTableName(TABLE_NAME));
    callResult->gasPricer()->appendOperation(InterfaceOpcode::OpenTable);
    // table is not exist, create it.
    if (!table)
    {
        table = createTable(_context, precompiled::getTableName(TABLE_NAME),
            KEY_FIELD, VALUE_FIELD, _origin);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::CreateTable);
        if (!table)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("set")
                << LOG_DESC("open table failed.");
            getErrorCodeOut(callResult->mutableExecResult(), storage::CODE_NO_AUTHORIZED);
            return callResult;
        }
        // init global model, 首次创建表的时候初始化模型
        InitGlobalModel(table, _origin, callResult);
    }
    if (func == name2Selector[REGISTER_NODE]) {           //节点注册
        std::string roles_str = GetVariable(table, _origin, callResult, ROLES_FIELD_NAME);
        Pair roles = json::parse(roles_str);
        // _origin_str 每个客户端特有的地址
        // 如果角色map中没有，为该客户端分配新角色
        if (roles.find(_origin_str) == roles.end()) {
            roles[_origin_str] = "trainer";

            // start FL training and select the committee randomly if there are enougth clients
            // 将对应数量的客户端分别注册为训练节点和委员会节点
            if (roles.size() == CLIENT_NUM) {
                int i = 1;
                for (auto& client : roles) {
                    if (i > COMM_COUNT)
                        break;
                    client.second = "comm";
                    i++;
                }
                int epoch = 0;
                std::string epoch_str = to_json_string(epoch);
                // 当所有的节点都注册完后，初始化训练次数为0
                UpdateVariable(table, _origin, callResult, EPOCH_FIELD_NAME, epoch_str);
            }

            // 更新注册的节点到表中
            roles_str = to_json_string(roles);
            UpdateVariable(table, _origin, callResult, ROLES_FIELD_NAME, roles_str);
        }
    }
    else if (func == name2Selector[QUERY_STATE]) {              //查询全局状态
       /*return (global_state['roles'].get(node_id, ROLE_TRAINER),
           global_state['epoch'],
           global_state['update_count'],
           global_state['score_count'])*/
        std::string roles_str = GetVariable(table, _origin, callResult, ROLES_FIELD_NAME);
        Pair roles = json::parse(roles_str);
        if (roles.find(_origin_str) == roles.end()) {
            roles[_origin_str] = "trainer";
        }

        std::string epoch_str = GetVariable(table, _origin, callResult, EPOCH_FIELD_NAME);
        int epoch = json::parse(epoch_str);

        // 返回当前节点角色和全局迭代次数
        callResult->setExecResult(abi.abiIn("", roles[_origin_str], s256(epoch)));

    }
    else if (func == name2Selector[QUERY_GLOBAL_MODEL]) {        //获取全局模型
        std::string global_model_str = GetVariable(table, _origin, callResult, GLOBAL_MODEL_FIELD_NAME);

        std::string epoch_str = GetVariable(table, _origin, callResult, EPOCH_FIELD_NAME);
        int epoch = json::parse(epoch_str);

        // 返回全局模型和全局epoch
        callResult->setExecResult(abi.abiIn("", global_model_str, s256(epoch)));

    }
    else if (func == name2Selector[UPLOAD_LOCAL_UPDATE]) {       //上传本地更新
       // get params
        std::string update;
        //signed int256
        s256 ep;
        // 解析参数，上传的本地更新和当前epoch
        abi.abiOut(data, update, ep);
        // 获取当前的全局epoch
        std::string epoch_str = GetVariable(table, _origin, callResult, EPOCH_FIELD_NAME);
        int epoch = json::parse(epoch_str);

        // not current epoch
        if (ep != epoch)
            return callResult;

        // 获取当前的更新列表
        std::string local_updates_str = GetVariable(table, _origin, callResult, LOCAL_UPDATES_FIELD_NAME);
        Pair local_updates = json::parse(local_updates_str);

        // local update is exist
        if (local_updates.find(_origin_str) != local_updates.end())
            return callResult;

        //获取已上传的本地更新数量
        std::string update_count_str = GetVariable(table, _origin, callResult, UPDATE_COUNT_FIELD_NAME);
        size_t update_count = json::parse(update_count_str);

        // if there are enougth updates
        if (update_count >= NEEDED_UPDATE_COUNT) {
#if OUTPUT
            std::clog << "the update of local model is not collected" << std::endl;
#endif              
            return callResult;
        }

        update_count += 1;
        update_count_str = to_json_string(update_count);

        local_updates[_origin_str] = update;
        local_updates_str = to_json_string(local_updates);

        // 保存当前本地更新和数量到表
        UpdateVariable(table, _origin, callResult, UPDATE_COUNT_FIELD_NAME, update_count_str);
        UpdateVariable(table, _origin, callResult, LOCAL_UPDATES_FIELD_NAME, local_updates_str);

#if OUTPUT
        std::clog << "the update of local model is collected" << std::endl;
#endif        

    }
    else if (func == name2Selector[UPLOAD_SCORES]) {             //上传分数
       // 0. get params
        s256 ep;
        std::string strValue;
        abi.abiOut(data, ep, strValue);

        // 1. if ep == epoch
        std::string epoch_str = GetVariable(table, _origin, callResult, EPOCH_FIELD_NAME);
        int epoch = json::parse(epoch_str);
        if (ep != epoch)
            return callResult;

        // 2. if client is the committee，委员会节点工作
        std::string roles_str = GetVariable(table, _origin, callResult, ROLES_FIELD_NAME);
        Pair roles = json::parse(roles_str);
        if (roles.find(_origin_str) == roles.end() || roles[_origin_str] == "trainer")
            return callResult;

        // 3. score_count + 1 and insert local_scores

        // 获取分数列表
        std::string local_scores_str = GetVariable(table, _origin, callResult, LOCAL_SCORES_FIELD_NAME);
        Pair local_scores = json::parse(local_scores_str);
        // 将分数加入表中
        local_scores[_origin_str] = strValue;
        local_scores_str = to_json_string(local_scores);
        UpdateVariable(table, _origin, callResult, LOCAL_SCORES_FIELD_NAME, local_scores_str);

        // 已上传的分数数量+1
        std::string score_count_str = GetVariable(table, _origin, callResult, SCORE_COUNT_FIELD_NAME);
        size_t score_count = json::parse(score_count_str);
        score_count += 1;
        score_count_str = to_json_string(score_count);
        UpdateVariable(table, _origin, callResult, SCORE_COUNT_FIELD_NAME, score_count_str);

#if OUTPUT
        std::clog << score_count << " scores has been uploaded" << std::endl;
#endif

        // 4. if all scores have been uploaded --> aggregate，所有委员会节点评分后，上链
        if (score_count == COMM_COUNT)
            Aggregate(table, _origin, callResult, local_scores);

    }
    else if (func == name2Selector[QUERY_ALL_UPDATES]) {         //获取所有本地更新
        std::string update_count_str = GetVariable(table, _origin, callResult, UPDATE_COUNT_FIELD_NAME);
        size_t update_count = json::parse(update_count_str);

        // if there are not enougth updates
        if (update_count < NEEDED_UPDATE_COUNT) {
            std::string res = "";
            callResult->setExecResult(abi.abiIn("", res));;
        }
        else {
            std::string local_updates_str = GetVariable(table, _origin, callResult, LOCAL_UPDATES_FIELD_NAME);
            callResult->setExecResult(abi.abiIn("", local_updates_str));
        }
    }
    else {// unknown function call
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CommitteePrecompiled") << LOG_DESC(" unknown func ")
            << LOG_KV("func", func);
        callResult->setExecResult(abi.abiIn("", u256(CODE_UNKNOW_FUNCTION_CALL)));
    }
    return callResult;
}

// init global model
void CommitteePrecompiled::InitGlobalModel(Table::Ptr table, Address const& _origin, PrecompiledExecResult::Ptr callResult) {
    int epoch = -999;
    std::string epoch_str = to_json_string(epoch);
    // 当当前迭代次数放入表中
    InsertVariable(table, _origin, callResult, EPOCH_FIELD_NAME, epoch_str);
    // 初始化话模型
    Model ser_model;
    // 将模型序列化，即转为字符串
    std::string ser_model_str = ser_model.to_json_string();
    // 将初始化的模型存入表中
    InsertVariable(table, _origin, callResult, GLOBAL_MODEL_FIELD_NAME, ser_model_str);
    // size_t node_count = 0;
    // std::string node_count_str = to_json_string(node_count);
    // InsertVariable(table, _origin, callResult, NODE_COUNT_FIELD_NAME, node_count_str);
    size_t update_count = 0;
    // 保存当前已上传本地更新的数量，初始化为0
    std::string update_count_str = to_json_string(update_count);
    InsertVariable(table, _origin, callResult, UPDATE_COUNT_FIELD_NAME, update_count_str);
    size_t score_count = 0;
    // 保存当前已上传评分的数量，初始化为0
    std::string score_count_str = to_json_string(score_count);
    InsertVariable(table, _origin, callResult, SCORE_COUNT_FIELD_NAME, score_count_str);
    // 储存多个角色对
    Pair roles;
    // 保存客户端的角色
    std::string roles_str = to_json_string(roles);
    InsertVariable(table, _origin, callResult, ROLES_FIELD_NAME, roles_str);
    //保存所有已上传的模型更新 (unordered_map<Adress,string>)
    Pair local_updates;
    std::string local_updates_str = to_json_string(local_updates);
    InsertVariable(table, _origin, callResult, LOCAL_UPDATES_FIELD_NAME, local_updates_str);
    //保存所有已上传的模型评分 (unordered_map<Adress,string>, string=unordered<Adress,float>)
    Pair local_scores;
    std::string local_scores_str = to_json_string(local_scores);
    InsertVariable(table, _origin, callResult, LOCAL_SCORES_FIELD_NAME, local_scores_str);
}

// aggregate on chain
void CommitteePrecompiled::Aggregate(Table::Ptr table, Address const& _origin, PrecompiledExecResult::Ptr callResult, Pair& comm_scores) {
    // 0. get the median as client's score
    std::unordered_map<std::string, std::vector<float>> local_scores;
    Scores scores; // typedef std::unordered_map<std::string, float> Scores;
    // comm_scores => 委员会分数 (unordered_map<Adress,string>, string=unordered<Adress,float>)
    for (auto& it1 : comm_scores) {
        // 每个委员会节点中有对所有训练节点的评分 => 为一个map <string, float>
        Scores trainer_scores = json::parse(it1.second);
        for (auto& it2 : trainer_scores) {
            // 对每个训练节点的评分
            local_scores[it2.first].push_back(it2.second);
        }
    }
    // 上面结束后为：local_scores中放的 train_1 = [98.1,89.1,95.,86.1],train_2 = [95.6,87.8,91.0.,86.1],...
    // 每个训练节点有个中间评分，如下
    for (auto& it : local_scores) {
        float s = GetMid(it.second);
        scores[it.first] = s;
    }
    // 上面结束后： scores = [<train_1:95.2>,<train_2:94.3>,...,<train_n:96.27]

    // 1. sort the trainer by their scores (or directly select the top k of trainers)
    std::vector<std::pair<std::string, float>> scores_vec(scores.begin(), scores.end());
    // 将训练节点安分数排序
    std::sort(scores_vec.begin(), scores_vec.end(), cmp_by_value);

    // 2. query all local updates
    std::string local_updates_str = GetVariable(table, _origin, callResult, LOCAL_UPDATES_FIELD_NAME);
    Pair local_updates = json::parse(local_updates_str);

    // 3. select the top k of trainers' updates to aggregate
    LocalUpdate total_update;
    // 获取每个客户端的地址
    auto trainer = scores_vec[0].first;
    // 每个本地客户端都有一个权重更新过来 (unordered_map<Adress,string>)
    std::string update_str = local_updates[trainer];
    LocalUpdate update = json::parse(update_str);
    auto& con_W_0 = update.delta_model.con_W_0;
    auto& con_b_0 = update.delta_model.con_b_0;
    auto& con_W_1 = update.delta_model.con_W_1;
    auto& con_b_1 = update.delta_model.con_b_1;
    auto& con_W_2 = update.delta_model.con_W_2;
    auto& con_b_2 = update.delta_model.con_b_2;
    auto& con_W_3 = update.delta_model.con_W_3;
    auto& con_b_3 = update.delta_model.con_b_3;
    auto& con_W_4 = update.delta_model.con_W_4;
    auto& con_b_4 = update.delta_model.con_b_4;
    auto& con_W_5 = update.delta_model.con_W_5;
    auto& con_b_5 = update.delta_model.con_b_5;
    auto& den_W_0 = update.delta_model.den_W_0;
    auto& den_b_0 = update.delta_model.den_b_0;
    // scores_vec中放的键值对
    for (int k = 1; k < AGGREGATE_COUNT; k++) {
        // 获取每个客户端的地址
        auto trainer = scores_vec[k].first;
        // 每个本地客户端都有一个权重更新过来 (unordered_map<Adress,string>)
        std::string update_str = local_updates[trainer];
        LocalUpdate update = json::parse(update_str);
        total_update.meta.n_samples += update.meta.n_samples;
        total_update.meta.avg_cost += update.meta.avg_cost;
        /*auto & delta_W = update.delta_model.ser_W;
        auto & delta_b = update.delta_model.ser_b;*/
        auto& con_W_0_str = update.delta_model.con_W_0;
        auto& con_b_0_str = update.delta_model.con_b_0;
        auto& con_W_1_str = update.delta_model.con_W_1;
        auto& con_b_1_str = update.delta_model.con_b_1;
        auto& con_W_2_str = update.delta_model.con_W_2;
        auto& con_b_2_str = update.delta_model.con_b_2;
        auto& con_W_3_str = update.delta_model.con_W_3;
        auto& con_b_3_str = update.delta_model.con_b_3;
        auto& con_W_4_str = update.delta_model.con_W_4;
        auto& con_b_4_str = update.delta_model.con_b_4;
        auto& con_W_5_str = update.delta_model.con_W_5;
        auto& con_b_5_str = update.delta_model.con_b_5;
        auto& den_W_0_str = update.delta_model.den_W_0;
        auto& den_b_0_str = update.delta_model.den_b_0;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; j++) {
                for (int k = 0; k < 3; k++) {
                    for (int l = 0; l < 64; k++) {
                        con_W_0[i][j][k][l] += con_W_0_str[i][j][k][l];
                        if (k == AGGREGATE_COUNT - 1) {
                            con_W_0[i][j][k][l] = con_W_0[i][j][k][l] / AGGREGATE_COUNT;
                        }
                    }
                }
            }
        }
        for (int i = 0; i < 64; i++) {
            con_b_0[i] += con_b_0_str[i];
            if (k == AGGREGATE_COUNT - 1) {
                con_b_0[i] = con_b_0[i] / AGGREGATE_COUNT;
            }
        }
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; j++) {
                for (int k = 0; k < 64; k++) {
                    for (int l = 0; l < 64; k++) {
                        con_W_1[i][j][k][l] += con_W_1_str[i][j][k][l];
                        if (k == AGGREGATE_COUNT - 1) {
                            con_W_1[i][j][k][l] = con_W_1[i][j][k][l] / AGGREGATE_COUNT;
                        }
                    }
                }
            }
        }
        for (int i = 0; i < 64; i++) {
            con_b_1[i] += con_b_1_str[i];
            if (k == AGGREGATE_COUNT - 1) {
                con_b_1[i] = con_b_1[i] / AGGREGATE_COUNT;
            }
        }
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; j++) {
                for (int k = 0; k < 64; k++) {
                    for (int l = 0; l < 128; k++) {
                        con_W_2[i][j][k][l] += con_W_2_str[i][j][k][l];
                        if (k == AGGREGATE_COUNT - 1) {
                            con_W_2[i][j][k][l] = con_W_2[i][j][k][l] / AGGREGATE_COUNT;
                        }
                    }
                }
            }
        }
        for (int i = 0; i < 128; i++) {
            con_b_2[i] += con_b_2_str[i];
            if (k == AGGREGATE_COUNT - 1) {
                con_b_2[i] = con_b_2[i] / AGGREGATE_COUNT;
            }
        }
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; j++) {
                for (int k = 0; k < 128; k++) {
                    for (int l = 0; l < 128; k++) {
                        con_W_3[i][j][k][l] += con_W_3_str[i][j][k][l];
                        if (k == AGGREGATE_COUNT - 1) {
                            con_W_3[i][j][k][l] = con_W_3[i][j][k][l] / AGGREGATE_COUNT;
                        }
                    }
                }
            }
        }
        for (int i = 0; i < 128; i++) {
            con_b_3[i] += con_b_3_str[i];
            if (k == AGGREGATE_COUNT - 1) {
                con_b_3[i] = con_b_3[i] / AGGREGATE_COUNT;
            }
        }
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; j++) {
                for (int k = 0; k < 128; k++) {
                    for (int l = 0; l < 256; k++) {
                        con_W_4[i][j][k][l] += con_W_4_str[i][j][k][l];
                        if (k == AGGREGATE_COUNT - 1) {
                            con_W_4[i][j][k][l] = con_W_4[i][j][k][l] / AGGREGATE_COUNT;
                        }
                    }
                }
            }
        }
        for (int i = 0; i < 256; i++) {
            con_b_4[i] += con_b_4_str[i];
            if (k == AGGREGATE_COUNT - 1) {
                con_b_4[i] = con_b_4[i] / AGGREGATE_COUNT;
            }
        }
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; j++) {
                for (int k = 0; k < 256; k++) {
                    for (int l = 0; l < 256; k++) {
                        con_W_5[i][j][k][l] += con_W_5_str[i][j][k][l];
                        if (k == AGGREGATE_COUNT - 1) {
                            con_W_5[i][j][k][l] = con_W_5[i][j][k][l] / AGGREGATE_COUNT;
                        }
                    }
                }
            }
        }
        for (int i = 0; i < 256; i++) {
            con_b_5[i] += con_b_5_str[i];
            if (k == AGGREGATE_COUNT - 1) {
                con_b_5[i] = con_b_5[i] / AGGREGATE_COUNT;
            }
        }
        for (int i = 0; i < 256; ++i) {
            for (int j = 0; j < 100; j++) {
                den_W_0[i][j] += den_W_0_str[i][j];
                if (k == AGGREGATE_COUNT - 1) {
                    den_W_0[i][j] = den_W_0[i][j] / AGGREGATE_COUNT;
                }
            }
        }
        for (int i = 0; i < 100; i++) {
            den_b_0[i] += den_b_0_str[i];
            if (k == AGGREGATE_COUNT - 1) {
                den_b_0[i] = den_b_0[i] / AGGREGATE_COUNT;
            }
        }
    }


    // 4. update global model and epoch, clear local updates and local socres, reset update count and score count
    std::string global_model_str = GetVariable(table, _origin, callResult, GLOBAL_MODEL_FIELD_NAME);
    Model global_model = json::parse(global_model_str);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                for (int l = 0; l < 64; k++) {
                    global_model.con_W_0[i][j][k][l] = con_W_0[i][j][k][l];
                }
            }
        }
    }
    for (int i = 0; i < 64; i++) {
        global_model.con_b_0[i] = con_b_0[i];
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 64; k++) {
                for (int l = 0; l < 64; k++) {
                    global_model.con_W_1[i][j][k][l] = con_W_1[i][j][k][l];
                }
            }
        }
    }
    for (int i = 0; i < 64; i++) {
        global_model.con_b_1[i] = con_b_1[i];
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 64; k++) {
                for (int l = 0; l < 128; k++) {
                    global_model.con_W_2[i][j][k][l] = con_W_2[i][j][k][l];
                }
            }
        }
    }
    for (int i = 0; i < 128; i++) {
        global_model.con_b_2[i] = con_b_2[i];
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 128; k++) {
                for (int l = 0; l < 128; k++) {
                    global_model.con_W_3[i][j][k][l] = con_W_3[i][j][k][l];
                }
            }
        }
    }
    for (int i = 0; i < 128; i++) {
        global_model.con_b_3[i] = con_b_3[i];
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 128; k++) {
                for (int l = 0; l < 256; k++) {
                    global_model.con_W_4[i][j][k][l] = con_W_4[i][j][k][l];
                }
            }
        }
    }
    for (int i = 0; i < 256; i++) {
        global_model.con_b_4[i] = con_b_4[i];
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 256; k++) {
                for (int l = 0; l < 256; k++) {
                    global_model.con_W_5[i][j][k][l] = con_W_5[i][j][k][l];
                }
            }
        }
    }
    for (int i = 0; i < 256; i++) {
        global_model.con_b_5[i] = con_b_5[i];
    }
    for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 100; j++) {
            global_model.den_W_0[i][j] = den_W_0[i][j];
        }
    }
    for (int i = 0; i < 100; i++) {
        global_model.den_b_0[i] = den_b_0[i];
    }
    global_model_str = global_model.to_json_string();
    UpdateVariable(table, _origin, callResult, GLOBAL_MODEL_FIELD_NAME, global_model_str);

    //更新epoch
    std::string epoch_str = GetVariable(table, _origin, callResult, EPOCH_FIELD_NAME);
    int epoch = json::parse(epoch_str);
    epoch += 1;
    epoch_str = to_json_string(epoch);
    UpdateVariable(table, _origin, callResult, EPOCH_FIELD_NAME, epoch_str);

    //清空表中的内容,重置
    local_updates.clear();
    local_updates_str = to_json_string(local_updates);
    UpdateVariable(table, _origin, callResult, LOCAL_UPDATES_FIELD_NAME, local_updates_str);

    comm_scores.clear();
    std::string local_scores_str = to_json_string(comm_scores);
    UpdateVariable(table, _origin, callResult, LOCAL_SCORES_FIELD_NAME, local_scores_str);

    size_t update_count = 0;
    std::string update_count_str = to_json_string(update_count);
    UpdateVariable(table, _origin, callResult, UPDATE_COUNT_FIELD_NAME, update_count_str);

    size_t score_count = 0;
    std::string score_count_str = to_json_string(score_count);
    UpdateVariable(table, _origin, callResult, SCORE_COUNT_FIELD_NAME, score_count_str);

    // 5. reset clients'roles
    std::string roles_str = GetVariable(table, _origin, callResult, ROLES_FIELD_NAME);
    Pair roles = json::parse(roles_str);
    for (auto& client : roles) {
        if (client.second == "comm")
            client.second = "trainer";
    }
    for (int k = 0; k < AGGREGATE_COUNT; k++) {
        auto trainer = scores_vec[k].first;
        roles[trainer] = "comm";
    }
    roles_str = to_json_string(roles);
    UpdateVariable(table, _origin, callResult, ROLES_FIELD_NAME, roles_str);
}

// insert variable
void CommitteePrecompiled::InsertVariable(Table::Ptr table, Address const& _origin, PrecompiledExecResult::Ptr callResult, const std::string& Key, std::string& strValue) {
    int count = 0;
    auto entry = table->newEntry();
    // 往表中插入键值对 key => value, 例如 "epoch" => -999
    entry->setField(KEY_FIELD, Key);
    entry->setField(VALUE_FIELD, strValue);
    // 插入成功返回1， 插入失败返回-5000
    count = table->insert(
        Key, entry, std::make_shared<AccessOptions>(_origin));
    if (count > 0)
    {
        callResult->gasPricer()->updateMemUsed(entry->capacity() * count);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Insert, count);
    }
    if (count == storage::CODE_NO_AUTHORIZED)
    {  //  permission denied
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CommitteePrecompiled") << LOG_DESC("set")
            << LOG_DESC("permission denied");
    }
    getErrorCodeOut(callResult->mutableExecResult(), count);
}

// get variable，返回键值Key对应的value
std::string CommitteePrecompiled::GetVariable(Table::Ptr table, Address const&, PrecompiledExecResult::Ptr callResult, const std::string& Key) {
    auto entries = table->select(Key, table->newCondition());
    std::string retValue = "";
    if (0u != entries->size())
    {
        callResult->gasPricer()->updateMemUsed(getEntriesCapacity(entries));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Select, entries->size());
        auto entry = entries->get(0);
        retValue = entry->getField(VALUE_FIELD);
    }
    return retValue;
}

// update variable 操作同插入
void CommitteePrecompiled::UpdateVariable(Table::Ptr table, Address const& _origin, PrecompiledExecResult::Ptr callResult, const std::string& Key, std::string& strValue) {
    int count = 0;
    auto entry = table->newEntry();
    entry->setField(KEY_FIELD, Key);
    entry->setField(VALUE_FIELD, strValue);
    count = table->update(
        Key, entry, table->newCondition(), std::make_shared<AccessOptions>(_origin));
    if (count > 0)
    {
        callResult->gasPricer()->updateMemUsed(entry->capacity() * count);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Update, count);
    }
    if (count == storage::CODE_NO_AUTHORIZED)
    {  //  permission denied
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CommitteePrecompiled") << LOG_DESC("set")
            << LOG_DESC("permission denied");
    }
    getErrorCodeOut(callResult->mutableExecResult(), count);
}