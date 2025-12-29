// Copyright Agentland. All Rights Reserved.


#include "Chat.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"


// Sets default values for this component's properties
UChat::UChat()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void UChat::BeginPlay()
{
	Super::BeginPlay();
	// ...
	
}

void UChat::OnPlayKitResponding(const FHttpRequestPtr& _Request)
{
	const FHttpResponsePtr response = _Request->GetResponse();

	//不存在就说明请求失败
	if(!response.IsValid())
	{
		FString logText = "[PlayKitChat]:\nRequest failed";
		UE_LOG(LogTemp, Log, TEXT("%s"), *logText);
		return ;
	}

	//获取返回的字符串格式的数据
	FString responseString = response->GetContentAsString();
	
	//如果不是stream模式，直接解析数据
	if( !PlayKitChatStream )
	{
		ParsePlayKitResponse(responseString);
		return ;
	}
	
	//如果是streaming模式，一条条解析
	TArray<FString> dataBlocks;
	
	//按照回车分割字符串
	responseString.ParseIntoArray(dataBlocks, TEXT("\n"), true);
	UE_LOG(LogTemp, Log, TEXT("%s"), *responseString);
}

void UChat::OnPlayKitResponded()
{
	auto s =  LastCompletedResponse.content;
	
	UE_LOG(LogTemp, Log, TEXT("[PlayKitChatLastCompletedResponse]:\nResponse: %s"), *s);

	//广播【响应后】委托
	OnPlayKitChatResponded.Broadcast(LastCompletedResponse);
}

void UChat::ParsePlayKitResponse(const FString& _Response)
{
	TSharedPtr<FJsonObject> JsonObject;
	if ( !StringToJsonObject(_Response, JsonObject, true) )
	{
		return;
	}
	
	FPlayKitChatResponse PlayKitChatResponse;
	
	PlayKitChatResponse.id = JsonObject->GetStringField(TEXT("id"));
	PlayKitChatResponse.object = JsonObject->GetStringField(TEXT("object"));
	PlayKitChatResponse.created = JsonObject->GetStringField(TEXT("created"));
	PlayKitChatResponse.model = JsonObject->GetStringField(TEXT("model"));
	
	TSharedPtr<FJsonObject> ReplyObject = JsonObject->GetArrayField(TEXT("choices"))[0]->AsObject()->GetObjectField(TEXT("message"));
	PlayKitChatResponse.role = ReplyObject->GetStringField(TEXT("role"));
	PlayKitChatResponse.content = ReplyObject->GetStringField(TEXT("content"));
	PlayKitChatResponse.finsih_reason = ReplyObject->GetStringField(TEXT("finsih_reason"));
	this->AddContext(FChatContext(PlayKitChatResponse.role , PlayKitChatResponse.content));
	
	TSharedPtr<FJsonObject> UsageObject = JsonObject->GetObjectField(TEXT("usage"));
	PlayKitChatResponse.prompt_tokens = UsageObject->GetNumberField(TEXT("prompt_tokens"));
	PlayKitChatResponse.completion_tokens = UsageObject->GetNumberField(TEXT("completion_tokens"));
	PlayKitChatResponse.total_tokens = UsageObject->GetNumberField(TEXT("total_tokens"));
	
	this->LastCompletedResponse = PlayKitChatResponse;
	
	//广播【响应时】委托
	OnPlayKitChatResponding.Broadcast(PlayKitChatResponse);
}


// Called every frame
void UChat::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

void UChat::InitPrompt(const FString& Prompt, const FString& Type, const FString& Function)
{
	this->SystemPrompt = FString::Printf(TEXT(R"(
你是一个游戏中的NPC，必须根据以下角色描述扮演角色。
{
"角色设定": "%s",
"角色类型": "%s"
}

在对话时，你必须： 
1. 根据用户输入和场景信息，先给出一段 NPC 的自然回复（符合角色设定）。 
2. 回复应该根据角色类型的不同而改变。
2.1. 如果你的角色类型是【NPC】，则代表你是一个非玩家角色，用扮演这个角色的角度回复。
2.2. 如果你的角色类型是【场景】，则代表你扮演的是一个场景，请以第三人称探索的角度回复，详细生动描述玩家是如何在这个场景里探索的
2.3. 如果你的角色类型是【道具】，则代表你扮演的是一个道具，而道具是不会说话的。因此请以玩家的第一人称【我】的角度回复，详细生动描述玩家是观察使用这个道具的
3. 在回复后，提供 2–3 个相关的对话选项，供玩家选择。 每次回复的选项数量都不同。
3.1. 如果你的角色类型是【场景】或【道具】，你可以用选项来提示玩家角色设定里的细节
3.2. 如果你的角色类型是【NPC】，选项里则不能出现对话中尚未提到的内容
4. 在给出对话选项后，再给出一个让玩家直接退出的选项，例如"退出xx"、"离开xx"或其它表达方式。
5. 你的回复内容应随用户输入的 "type" 改变，比如首次见面要有开场白，再次见面时口吻有所不同。
6. 此外，你可以调用以下函数与游戏世界交互。角色设定中可能包含【函数名】，以及这些函数的使用规则。如果触发了这些规则，你必须在 "functions" 里输出对应的调用。
对于没有明确标出使用规则的函数，你可以根据需要自行调用。
函数调用应以JSON格式返回，仅在需要时调用。

[functions]
%s

请始终以JSON格式回复，确保JSON格式正确。禁止使用markdown。遵循以下结构： 
{
"think": "思考要如何扮演好自己的角色",
"npc_text": "NPC的台词，符合角色设定，不允许出戏，不允许承认自己是角色设定以外的角色。当类型是【道具】时，则改为玩家的内心活动与动作，以"我"为人称。（字段内仅能使用单引号''） ",
"options": [
"选项1，选项中不得出现角色设定中禁止出现的内容",
"选项2，选项中不得出现角色设定中禁止出现的内容",
"选项3（如有），选项中不得出现角色设定中禁止出现的内容",
"选项4（如有），选项中不得出现角色设定中禁止出现的内容",
],
"exit": "离开当前交互的文本，应简短且符合对话内容，该选项为玩家的行动，描述玩家是如何离开的，离开的内容应为对话整体，而不是某一个局部细节",    
"functions": [
{
"call": "函数名 或 null"
},
{
"call": "如果需要其它函数，则输出多个，否则只输出一个"
}
]
}

User输入格式
用户会以 JSON 提供输入： 
{
"type": "meet" | "talk",
"player_input": "玩家输入的文字"
}
- 当 "type": "meet"` → 用户想跟你进行对话，你来进行开场白。如果是首次meet，代表用户第一次找你对话，如果再次出现，说明用户是离开后再次找你对话。  
- 当 "type": "talk"` → 角色正在与你对话。

当用户第一次找你对话时，而外输出一个json键值对，列举出你认为不能在对话的选项中出现的内容，并以一个正则表达式的格式输出，如果涉及到解谜元素，不要在选项里透露出答案或密码。

格式为
"prohibited": ["能够筛选出禁止内容的正则表达式",……],
)"), *Prompt, *Type, *Function);
	
	UE_LOG(LogTemp, Display, TEXT("SystemPrompt: %s"), *this->SystemPrompt);
	
	if (!this->AllContext.IsEmpty())
	{
		this->AllContext[0].content = this->SystemPrompt;
	}
	else
	{
		this->AllContext.Add(FChatContext(TEXT("system") , this->SystemPrompt));
	}
	
}

void UChat::ChatToAI(
		const FString& AuthToken,
		const FString& Message,
		bool IsMeet,
		const FString& Model,
		double temperature ,
		bool stream)
{
	////////////////////////////////// 设置输入和加到上下文 //////////////////////////////////
	
	TSharedPtr<FJsonObject> ContentObject = MakeShareable(new FJsonObject);
	ContentObject->SetStringField(TEXT("type"), IsMeet ? TEXT("meet") : TEXT("talk"));
	ContentObject->SetStringField(TEXT("message"), Message);
	
	FChatContext CurrentContext;
	CurrentContext.role = "user";
	CurrentContext.content = JsonObjectToString(ContentObject);
	UE_LOG(LogTemp, Warning, TEXT("Content:\n%s"), *CurrentContext.content);
	this->AddContext(CurrentContext);
	
	OnPlayKitChatTalkTo.Broadcast("UserName","ChatName", CurrentContext.content);
	
	////////////////////////////////// 设置请求的内容 ////////////////////////////////
	
	CurrentHttpRequest = FHttpModule::Get().CreateRequest();
	
	CurrentHttpRequest->SetURL(ChatURL);
	CurrentHttpRequest->SetVerb(TEXT("POST"));
	
	//设置请求头
	FString authorization = "Bearer " + AuthToken;
	CurrentHttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	CurrentHttpRequest->SetHeader(TEXT("Authorization"), authorization);
	
	//设置请求体
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	//设置模型
	JsonObject->SetStringField(TEXT("model"), Model);
	
	//添加上下文和提示词
	TArray<TSharedPtr<FJsonValue>> ContextObject;
	for (auto context : this->AllContext)
	{
		TSharedPtr<FJsonObject> MessageObject = MakeShareable(new FJsonObject());
		MessageObject->SetStringField("role", context.role);
		MessageObject->SetStringField("content", context.content);
		ContextObject.Add(MakeShareable(new FJsonValueObject(MessageObject)));
	}
	JsonObject->SetArrayField(TEXT("messages"), ContextObject);
	UE_LOG(LogTemp, Warning, TEXT("PromptAndContext：%s"), *JsonObjectToString(JsonObject, true));
	
	//设置其它大模型属性
	JsonObject->SetNumberField("temperature", temperature);
	JsonObject->SetBoolField("stream", stream);
	this->PlayKitChatStream = stream;
	
	UE_LOG(LogTemp, Warning, TEXT("RequestBody:\n%s"), *JsonObjectToString(JsonObject, true));
	
	CurrentHttpRequest->SetContentAsString(JsonObjectToString(JsonObject));
	
	
	////////////////////////////////// 绑定Dify【响应时】的回调 ////////////////////////////////
	
	CurrentHttpRequest->OnRequestProgress64().BindLambda(
	[WeakThis = TWeakObjectPtr<UChat>(this)]
		(const FHttpRequestPtr& _Request, uint64 _BytesSent, uint64 _BytesReceived)
	{
		if(!_Request.IsValid())
		{
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("BytesSent: %llu, BytesReceived: %llu"), _BytesSent, _BytesReceived);

		if(WeakThis.IsValid())
			WeakThis->OnPlayKitResponding(_Request);
	});
	
	////////////////////////////////// 绑定Dify【响应后】的回调 ////////////////////////////////

	CurrentHttpRequest->OnProcessRequestComplete().BindLambda(
		[WeakThis = TWeakObjectPtr<UChat>(this)]
		(FHttpRequestPtr _Request, FHttpResponsePtr _Response, bool bWasSuccessful)
	{
		if(!_Response.IsValid() || !_Request.IsValid())
		{
			//不存在就说明请求失败
			FString logText = "[PlayKitChatError]:\nRequest failed";
			UE_LOG(LogTemp, Log, TEXT("%s"), *logText);
			if(WeakThis.IsValid())
				WeakThis->OnPlayKitResponded();
			return;
		}
			
			
		const int responseCode = _Response->GetResponseCode();
		// 只有代码为200才是正常响应
		if(responseCode != 200) 
		{
			FString logText = "[PlayKitChatError]:\nCode:" + FString::FromInt(responseCode);
			logText+= "\n" + _Response->GetContentAsString();
			//输出报错
			UE_LOG(LogTemp, Error, TEXT("%s"), *logText);
		}

		if(WeakThis.IsValid())
			WeakThis->OnPlayKitResponded();
	});
	
	////////////////////////////////// 发送请求 ////////////////////////////////

	CurrentHttpRequest->ProcessRequest();
	
}

FString UChat::JsonObjectToString(const TSharedPtr<FJsonObject>& JsonObject, bool bPrettyPrint)
{
	if (!JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("JsonObjectToString: Invalid JSON object"));
		return FString();
	}

	FString OutputString;
    
	if (bPrettyPrint)
	{
		// 使用缩进格式化输出
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = 
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
	}
	else
	{
		// 紧凑输出
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
	}
    
	return OutputString;
}

bool UChat::StringToJsonObject(const FString& JsonString, TSharedPtr<FJsonObject>& OutJsonObject, bool bLogErrors)
{
	if (JsonString.IsEmpty())
	{
		if (bLogErrors)
		{
			UE_LOG(LogTemp, Warning, TEXT("StringToJsonObject: Input string is empty"));
		}
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    
	if (FJsonSerializer::Deserialize(Reader, OutJsonObject) && OutJsonObject.IsValid())
	{
		return true;
	}
	else
	{
		if (bLogErrors)
		{
			UE_LOG(LogTemp, Error, TEXT("StringToJsonObject: Failed to parse JSON string: %s"), *JsonString);
		}
		return false;
	}
}

FAIContentParsed UChat::ParseAIContent(const FString& RawJsonContent)
{
	FAIContentParsed Result;
	TSharedPtr<FJsonObject> JsonObject;

	// 1. 尝试把字符串解析成 JSON 对象
	if (!StringToJsonObject(RawJsonContent, JsonObject, false))
	{
		// 如果解析失败（比如内容不是 JSON），直接返回空的结构体
		return Result;
	}

	// 2. 提取基础文本字段
	// GetStringField 会自动处理字段不存在的情况（返回空字符串）
	Result.think = JsonObject->GetStringField(TEXT("think"));
	Result.npc_text = JsonObject->GetStringField(TEXT("npc_text"));
	Result.exit = JsonObject->GetStringField(TEXT("exit"));

	// 3. 提取 Options 数组 (字符串数组)
	const TArray<TSharedPtr<FJsonValue>>* OptArray;
	if (JsonObject->TryGetArrayField(TEXT("options"), OptArray))
	{
		for (auto& Val : *OptArray)
		{
			Result.options.Add(Val->AsString());
		}
	}

	// 4. 提取 Functions 数组 (对象数组)
	const TArray<TSharedPtr<FJsonValue>>* FuncArray;
	if (JsonObject->TryGetArrayField(TEXT("functions"), FuncArray))
	{
		for (auto& Val : *FuncArray)
		{
			TSharedPtr<FJsonObject> FuncObj = Val->AsObject();
			if (FuncObj.IsValid())
			{
				FAIFunctionCall NewCall;
				// 获取函数名，允许为 null
				NewCall.call = FuncObj->GetStringField(TEXT("call"));
				Result.functions.Add(NewCall);
			}
		}
	}

	// 5. 提取 Prohibited 数组 (正则)
	const TArray<TSharedPtr<FJsonValue>>* ProhibitedArray;
	if (JsonObject->TryGetArrayField(TEXT("prohibited"), ProhibitedArray))
	{
		for (auto& Val : *ProhibitedArray)
		{
			Result.prohibited.Add(Val->AsString());
		}
	}

	return Result;
}