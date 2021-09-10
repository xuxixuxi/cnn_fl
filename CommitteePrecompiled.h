#pragma once
#include <libprecompiled/Common.h>
#include <libnlohmann_json/single_include/nlohmann/json.hpp>
#define OUTPUT 1

// the setting of model
#define n_features 5
#define n_class 2

// the number of committee
#define COMM_COUNT 4
// the number of clients which to be aggregated
#define AGGREGATE_COUNT 6
// the number of needed update
#define NEEDED_UPDATE_COUNT 10
// the number of clients which have been register
#define CLIENT_NUM 20
// learning rate
#define learning_rate 0.001 

using json=nlohmann::json;

// global model or local model
struct Model
{
public:
    /* model data */
    std::vector<std::vector<std::vector<std::vector<float>>>> con_W_0;
    std::vector<float> con_b_0;
    std::vector<std::vector<std::vector<std::vector<float>>>> con_W_1;
    std::vector<float> con_b_1;
    std::vector<std::vector<std::vector<std::vector<float>>>> con_W_2;
    std::vector<float> con_b_2;
    std::vector<std::vector<std::vector<std::vector<float>>>> con_W_3;
    std::vector<float> con_b_3;
    std::vector<std::vector<std::vector<std::vector<float>>>> con_W_4;
    std::vector<float> con_b_4;
    std::vector<std::vector<std::vector<std::vector<float>>>> con_W_5;
    std::vector<float> con_b_5;
    std::vector<std::vector<float>> den_W_0;
    std::vector<float> den_b_0;

    /* constructor function */
    Model() {
        con_W_0 = std::vector<std::vector<std::vector<std::vector<float>>>>(3, std::vector<std::vector<std::vector<float>>>(3, std::vector<std::vector<float>>(3, std::vector<float>(64, 0))));
        con_b_0 = std::vector<float>(64, 0);
        con_W_1 = std::vector<std::vector<std::vector<std::vector<float>>>>(3, std::vector<std::vector<std::vector<float>>>(3, std::vector<std::vector<float>>(64, std::vector<float>(64, 0))));
        con_b_1 = std::vector<float>(64, 0);
        con_W_2 = std::vector<std::vector<std::vector<std::vector<float>>>>(3, std::vector<std::vector<std::vector<float>>>(3, std::vector<std::vector<float>>(64, std::vector<float>(128, 0))));
        con_b_2 = std::vector<float>(128, 0);
        con_W_3 = std::vector<std::vector<std::vector<std::vector<float>>>>(3, std::vector<std::vector<std::vector<float>>>(3, std::vector<std::vector<float>>(128, std::vector<float>(128, 0))));
        con_b_3 = std::vector<float>(128, 0);
        con_W_4 = std::vector<std::vector<std::vector<std::vector<float>>>>(3, std::vector<std::vector<std::vector<float>>>(3, std::vector<std::vector<float>>(128, std::vector<float>(256, 0))));
        con_b_4 = std::vector<float>(256, 0);
        con_W_5 = std::vector<std::vector<std::vector<std::vector<float>>>>(3, std::vector<std::vector<std::vector<float>>>(3, std::vector<std::vector<float>>(256, std::vector<float>(256, 0))));
        con_b_5 = std::vector<float>(256, 0);
        den_W_0 = std::vector<std::vector<float>>(256, std::vector<float>(100, 0));
        den_b_0 = std::vector<float>(100, 0);
    }

    Model(const json& j) {
        con_W_0 = j["conv2d/kernel:0"].get<std::vector<std::vector<std::vector<std::vector<float>>>>>();
        con_b_0 = j["conv2d/bias:0"].get<std::vector<float>>();
        con_W_1 = j["conv2d_1/kernel:0"].get<std::vector<std::vector<std::vector<std::vector<float>>>>>();
        con_b_1 = j["conv2d_1/bias:0"].get<std::vector<float>>();
        con_W_2 = j["conv2d_2/kernel:0"].get<std::vector<std::vector<std::vector<std::vector<float>>>>>();
        con_b_2 = j["conv2d_2/bias:0"].get<std::vector<float>>();
        con_W_3 = j["conv2d_3/kernel:0"].get<std::vector<std::vector<std::vector<std::vector<float>>>>>();
        con_b_3 = j["conv2d_3/bias:0"].get<std::vector<float>>();
        con_W_4 = j["conv2d_4/kernel:0"].get<std::vector<std::vector<std::vector<std::vector<float>>>>>();
        con_b_4 = j["conv2d_4/bias:0"].get<std::vector<float>>();
        con_W_5 = j["conv2d_5/kernel:0"].get<std::vector<std::vector<std::vector<std::vector<float>>>>>();
        con_b_5 = j["conv2d_5/bias:0"].get<std::vector<float>>();
        den_W_0 = j["dense/kernel:0"].get<std::vector<std::vector<float>>>();
        den_b_0 = j["dense/bias:0"].get<std::vector<float>>();
    }

    void operator=(const json& j) {
        con_W_0 = j["conv2d/kernel:0"].get<std::vector<std::vector<std::vector<std::vector<float>>>>>();
        con_b_0 = j["conv2d/bias:0"].get<std::vector<float>>();
        con_W_1 = j["conv2d_1/kernel:0"].get<std::vector<std::vector<std::vector<std::vector<float>>>>>();
        con_b_1 = j["conv2d_1/bias:0"].get<std::vector<float>>();
        con_W_2 = j["conv2d_2/kernel:0"].get<std::vector<std::vector<std::vector<std::vector<float>>>>>();
        con_b_2 = j["conv2d_2/bias:0"].get<std::vector<float>>();
        con_W_3 = j["conv2d_3/kernel:0"].get<std::vector<std::vector<std::vector<std::vector<float>>>>>();
        con_b_3 = j["conv2d_3/bias:0"].get<std::vector<float>>();
        con_W_4 = j["conv2d_4/kernel:0"].get<std::vector<std::vector<std::vector<std::vector<float>>>>>();
        con_b_4 = j["conv2d_4/bias:0"].get<std::vector<float>>();
        con_W_5 = j["conv2d_5/kernel:0"].get<std::vector<std::vector<std::vector<std::vector<float>>>>>();
        con_b_5 = j["conv2d_5/bias:0"].get<std::vector<float>>();
        den_W_0 = j["dense/kernel:0"].get<std::vector<std::vector<float>>>();
        den_b_0 = j["dense/bias:0"].get<std::vector<float>>();
    }

    std::string to_json_string() {
        json j;
        j["conv2d/kernel:0"] = con_W_0;
        j["conv2d/bias:0"] = con_b_0;
        j["conv2d_1/kernel:0"] = con_W_1;
        j["conv2d_1/bias:0"] = con_b_1;
        j["conv2d_2/kernel:0"] = con_W_2;
        j["conv2d_2/bias:0"] = con_b_2;
        j["conv2d_3/kernel:0"] = con_W_3;
        j["conv2d_3/bias:0"] = con_b_3;
        j["conv2d_4/kernel:0"] = con_W_4;
        j["conv2d_4/bias:0"] = con_b_4;
        j["conv2d_5/kernel:0"] = con_W_5;
        j["conv2d_5/bias:0"] = con_b_5;
        j["dense/kernel:0"] = den_W_0;
        j["dense/bias:0"] = den_b_0;
        return j.dump();
    }
};
// the meta of local model
struct Meta
{
	/* meta data */
	int n_samples;
	float avg_cost;

	/* constructor function */ 
	Meta(int n = 0, float cost = 0) : n_samples(n), avg_cost(cost){}

	Meta(const json & j){
        n_samples = j["n_samples"].get<int>();
        avg_cost = j["avg_cost"].get<float>();
    }

    void operator=(const json & j){
        n_samples = j["n_samples"].get<int>();
        avg_cost = j["avg_cost"].get<float>();
    }

    std::string to_json_string(){
        json j;
        j["n_samples"] = n_samples;
        j["avg_cost"] = avg_cost;
        return j.dump();
    }
};

// the update of local model
struct LocalUpdate
{
	/* update data */
	Model delta_model;
	Meta meta;

	/* constructor function */
	LocalUpdate(){}

	LocalUpdate(const json & j){
        delta_model = j["delta_model"];
        meta = j["meta"];
    }

    void operator=(const json & j){
        delta_model = j["delta_model"];
        meta = j["meta"];
    }

    std::string to_json_string(){
        json j;
        j["delta_model"] = delta_model.to_json_string();
        j["meta"] = meta.to_json_string();
        return j.dump();
    }
};

namespace dev
{
	namespace precompiled
	{

		class CommitteePrecompiled : public dev::precompiled::Precompiled
		{
		public:
			typedef std::shared_ptr<CommitteePrecompiled> Ptr;  //指针
			CommitteePrecompiled();   //构造
			virtual ~CommitteePrecompiled(){};   //析构
			
			//call函数有三个参数，
			//_context保存交易执行的上下文，
			//_param是调用合约的参数信息，
			//本次调用对应合约接口以及接口的参数可以从_param解析获取，_origin是交易发送者，用于权限控制。
			PrecompiledExecResult::Ptr call(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
					bytesConstRef _param, Address const& _origin = Address(),
					Address const& _sender = Address()) override;     //重载call函数

		private:
			void InitGlobalModel(storage::Table::Ptr table, Address const& _origin, PrecompiledExecResult::Ptr callResult);
			void Aggregate(storage::Table::Ptr table, Address const& _origin, PrecompiledExecResult::Ptr callResult, std::unordered_map<std::string, std::string>& local_scores);
			void InsertVariable(storage::Table::Ptr table, Address const& _origin, PrecompiledExecResult::Ptr callResult, const std::string & Key, std::string & strValue);
			std::string GetVariable(storage::Table::Ptr table, Address const& _origin, PrecompiledExecResult::Ptr callResult, const std::string & Key);
			void UpdateVariable(storage::Table::Ptr table, Address const& _origin, PrecompiledExecResult::Ptr callResult, const std::string & Key, std::string & strValue);
		};
	}  // namespace precompiled
}  // namespace dev