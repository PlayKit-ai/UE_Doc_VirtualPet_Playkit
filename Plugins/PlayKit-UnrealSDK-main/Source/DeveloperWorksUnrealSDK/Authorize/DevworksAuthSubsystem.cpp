// Copyright Agentland. All Rights Reserved.


#include "DevworksAuthSubsystem.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/BufferArchive.h"


void UDevworksAuthSubsystem::BeginDestroy()
{
	Super::BeginDestroy();

	// 销毁所有请求
	if(RequestCodeHttpRequest.IsValid())
	{
		RequestCodeHttpRequest->CancelRequest();
		RequestCodeHttpRequest.Reset();
	}
}

UDevworksAuthSubsystem* UDevworksAuthSubsystem::Get()
{
	return 
		GEngine->GameViewport->GetGameInstance()->GetSubsystem<UDevworksAuthSubsystem>();
}

//-----------------------------------------
// Purpose: 发送验证码
//-----------------------------------------
void UDevworksAuthSubsystem::RequestCode(const FString _Address, ELoginType _LoginType,
                                      FOnRequestCodeCompleted _OnRequestCodeCompleted)
{
	UDevworksAuthSubsystem* subsystem = 
		GEngine->GameViewport->GetGameInstance()->GetSubsystem<UDevworksAuthSubsystem>();

	// 务必存在
	check(subsystem);

	if(subsystem->RequestCodeHttpRequest.IsValid())
	{
		const EHttpRequestStatus::Type status =
			subsystem->RequestCodeHttpRequest->GetStatus();

		// 如果正在处理上次查询中，什么也不做
		if(status == EHttpRequestStatus::Processing)
			return ;
	}


	const FString url = subsystem->BaseURL / "api/auth/send-code";

	
	///////////////// 设置header /////////////////
	
	subsystem->RequestCodeHttpRequest = FHttpModule::Get().CreateRequest();

	
	subsystem->RequestCodeHttpRequest->SetURL(url);
	subsystem->RequestCodeHttpRequest->SetVerb("POST");
	subsystem->RequestCodeHttpRequest->SetHeader("Content-Type", "application/json");
	
	///////////////// 设置body /////////////////

	TSharedPtr<FJsonObject> jsonObject = MakeShared<FJsonObject>();
	jsonObject->SetStringField("identifier", _Address);
	jsonObject->SetStringField("type",
	                           _LoginType == ELoginType::Email ? "email" : "phone");
	FString jsonString;
	const TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&jsonString);
	FJsonSerializer::Serialize(jsonObject.ToSharedRef(), writer);
	subsystem->RequestCodeHttpRequest->SetContentAsString(jsonString);


	///////////////// 设置回调委托 ///////////////// 
	
	subsystem->RequestCodeHttpRequest->OnProcessRequestComplete().BindLambda(
		[WeakThis = TWeakObjectPtr(subsystem),_OnRequestCodeCompleted,_LoginType]
		(FHttpRequestPtr _Request, const FHttpResponsePtr& _Response, bool _bWasSuccessful)
		{
			// 防炸补丁
			if(!WeakThis.IsValid())
				return ;
			
			// 基本上都是网络错误
			if(!_bWasSuccessful || !_Response.IsValid())
			{
				(void)_OnRequestCodeCompleted.ExecuteIfBound(ERequestCodeStatus::NetworkError);
				return;
			}

			// 解析响应
			TSharedPtr<FJsonObject> jsonObject;
			TSharedRef<TJsonReader<>> reader = TJsonReaderFactory<>::Create(_Response->GetContentAsString());

			// 卡脖子了
			if(!FJsonSerializer::Deserialize(reader, jsonObject))
			{
				(void)_OnRequestCodeCompleted.ExecuteIfBound(ERequestCodeStatus::UnknownError);
				return;
			}
			
			const int32 responseCode = _Response->GetResponseCode();
			
			// 如果是400 or 500
			if(responseCode == 400 || responseCode == 500)
			{
				const FString code = jsonObject->GetStringField(TEXT("code"));

				// 手机号或邮箱格式错误
				if(code.Equals("VALIDATION_ERROR"))
				{
					(void)_OnRequestCodeCompleted.ExecuteIfBound(
						_LoginType == ELoginType::Email?
						ERequestCodeStatus::InvalidEmail : ERequestCodeStatus::InvalidPhone);
					return ;
				}
				// 通常也是手机号或邮箱格式错误
				if(code.Equals("PROVIDER_ERROR"))
				{
					(void)_OnRequestCodeCompleted.ExecuteIfBound(
						_LoginType == ELoginType::Email?
						ERequestCodeStatus::InvalidEmail : ERequestCodeStatus::InvalidPhone);
					return ;
				}
				// 手机号或邮箱不能为空
				if(code.Equals("MISSING_PARAMETERS"))
				{
					(void)_OnRequestCodeCompleted.ExecuteIfBound(
						ERequestCodeStatus::MissingParam);
					return ;
				}
			}
			// 429 错误次数太多
			if(responseCode == 429)
			{
				(void)_OnRequestCodeCompleted.ExecuteIfBound(ERequestCodeStatus::TooMany);
				return ;
			}
			// 如果200
			if(responseCode == 200)
			{
				WeakThis.Get()->RequestCodeSessionId = jsonObject->GetStringField(TEXT("sessionId"));

				// 一般情况不为空
				if(WeakThis.Get()->RequestCodeSessionId.IsEmpty())
				{
					(void)_OnRequestCodeCompleted.ExecuteIfBound(ERequestCodeStatus::NetworkError);
					return ;
				}

				// 发送成功,等待接收验证码
				(void)_OnRequestCodeCompleted.ExecuteIfBound(ERequestCodeStatus::Success);
				return ;
			}

			// 如果到这里还没有返回,就说明是未知错误
			(void)_OnRequestCodeCompleted.ExecuteIfBound(ERequestCodeStatus::UnknownError);
			return ;
		});
	
	////////////////// 发送请求 //////////////////
	subsystem->RequestCodeHttpRequest->ProcessRequest();
}


//-----------------------------------------
// Purpose: 验证-验证码
//-----------------------------------------
void UDevworksAuthSubsystem::VerifyCode(const FString _Code,FOnVerifyCodeCompleted _OnVerifyCodeCompleted)
{
	UDevworksAuthSubsystem* subsystem = 
		GEngine->GameViewport->GetGameInstance()->GetSubsystem<UDevworksAuthSubsystem>();

	// 务必存在
	check(subsystem);

	if(subsystem->VerifyCodeHttpRequest.IsValid())
	{
		const EHttpRequestStatus::Type status =
			subsystem->VerifyCodeHttpRequest->GetStatus();

		// 如果正在处理上次查询中，什么也不做
		if(status == EHttpRequestStatus::Processing)
			return ;
	}
	
	const FString url = subsystem->BaseURL / "/api/auth/verify-code";
	
	///////////////// 设置header /////////////////
	subsystem->VerifyCodeHttpRequest = FHttpModule::Get().CreateRequest();
	
	subsystem->VerifyCodeHttpRequest->SetURL(url);
	subsystem->VerifyCodeHttpRequest->SetVerb("POST");
	subsystem->VerifyCodeHttpRequest->SetHeader("Content-Type", "application/json");
	///////////////// 设置body /////////////////

	TSharedPtr<FJsonObject> jsonObject = MakeShared<FJsonObject>();
	jsonObject->SetStringField("sessionId", subsystem->RequestCodeSessionId);
	jsonObject->SetStringField("code",_Code);
	
	FString jsonString;
	const TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&jsonString);
	FJsonSerializer::Serialize(jsonObject.ToSharedRef(), writer);
	subsystem->VerifyCodeHttpRequest->SetContentAsString(jsonString);

	///////////////// 设置回调委托 ///////////////// 
	
	subsystem->VerifyCodeHttpRequest->OnProcessRequestComplete().BindLambda(
		[WeakThis = TWeakObjectPtr(subsystem),_OnVerifyCodeCompleted]
		(FHttpRequestPtr _Request, const FHttpResponsePtr& _Response, bool _bWasSuccessful)
		{
			// 防炸补丁
			if(!WeakThis.IsValid())
				return ;
			
			// 基本上都是网络错误
			if(!_bWasSuccessful || !_Response.IsValid())
			{
				(void)_OnVerifyCodeCompleted.ExecuteIfBound(EVerifyCodeStatus::NetworkError);
				return;
			}

			// 解析响应
			TSharedPtr<FJsonObject> newJsonObject;
			const TSharedRef<TJsonReader<>> reader =
				TJsonReaderFactory<>::Create(_Response->GetContentAsString());

			// 卡脖子了
			if(!FJsonSerializer::Deserialize(reader, newJsonObject))
			{
				(void)_OnVerifyCodeCompleted.ExecuteIfBound(EVerifyCodeStatus::UnknownError);
				return;
			}
			
			const int32 responseCode = _Response->GetResponseCode();
			
			// 如果是400直接“无效验证码”
			if(responseCode == 400 )
			{
				FString message = newJsonObject->GetStringField(TEXT("message"));

				// 典型的验证码错误
				if(message.Equals("Invalid verification code"))
				{
					(void)_OnVerifyCodeCompleted.ExecuteIfBound(
						EVerifyCodeStatus::InvalidCode);
					return ;
				}

				// 验证码过期
				if(message.Contains("expired"))
				{
					(void)_OnVerifyCodeCompleted.ExecuteIfBound(
						EVerifyCodeStatus::Expired);
					return ;
				}
				
				// 其它情况就直接“无效验证码”
				(void)_OnVerifyCodeCompleted.ExecuteIfBound(
					EVerifyCodeStatus::InvalidCode);
				return ;
			}

			// 429 错误次数太多
			if(responseCode == 429)
			{
				(void)_OnVerifyCodeCompleted.ExecuteIfBound(
					EVerifyCodeStatus::TooMany);
				return ;
			}
			// 如果200
			if(responseCode == 200)
			{
				FString userId = newJsonObject->GetStringField(TEXT("userId"));

				FString globalToken = newJsonObject->GetStringField(TEXT("globalToken"));

				(void)_OnVerifyCodeCompleted.ExecuteIfBound(
					EVerifyCodeStatus::Success);
				// 获取玩家token
				WeakThis->GetPlayerToken(globalToken,_OnVerifyCodeCompleted);
				return ;
			}

			// 如果到这里还没有返回,就说明是未知错误
			(void)_OnVerifyCodeCompleted.ExecuteIfBound(EVerifyCodeStatus::UnknownError);
			return ;
		});

	////////////////// 发送请求 //////////////////
	subsystem->VerifyCodeHttpRequest->ProcessRequest();
}

//----------------------------------------
// Purpose: 获取玩家token
//----------------------------------------
void UDevworksAuthSubsystem::GetPlayerToken(const FString& _GlobalToken,FOnVerifyCodeCompleted _OnVerifyCodeCompleted)
{
	UDevworksAuthSubsystem* subsystem = 
		GEngine->GameViewport->GetGameInstance()->GetSubsystem<UDevworksAuthSubsystem>();

	// 务必存在
	check(subsystem);

	if(subsystem->GetPlayerTokenHttpRequest.IsValid())
	{
		const EHttpRequestStatus::Type status =
			subsystem->GetPlayerTokenHttpRequest->GetStatus();

		// 如果正在处理上次查询中，什么也不做
		if(status == EHttpRequestStatus::Processing)
		{
			(void)_OnVerifyCodeCompleted.ExecuteIfBound(EVerifyCodeStatus::TooMany);
			return;
		}
		
	}

	const FString url = subsystem->BaseURL / "/api/external/exchange-jwt";

	///////////////// 设置header /////////////////
	subsystem->GetPlayerTokenHttpRequest = FHttpModule::Get().CreateRequest();
	
	subsystem->GetPlayerTokenHttpRequest->SetURL(url);
	subsystem->GetPlayerTokenHttpRequest->SetVerb("POST");

	// TODO: 加Bearer反而可能会报错,待复查
	FString authorization = "Bearer " + _GlobalToken;
	subsystem->GetPlayerTokenHttpRequest->SetHeader("Authorization", authorization);
	subsystem->GetPlayerTokenHttpRequest->SetHeader("Content-Type", "application/json");
	///////////////// 设置body /////////////////
	TSharedPtr<FJsonObject> jsonObject = MakeShared<FJsonObject>();
	jsonObject->SetStringField("jwt", _GlobalToken);

	FString jsonString;
	const TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&jsonString);
	FJsonSerializer::Serialize(jsonObject.ToSharedRef(), writer);
	subsystem->GetPlayerTokenHttpRequest->SetContentAsString(jsonString);

	///////////////// 设置回调委托 /////////////////
	subsystem->GetPlayerTokenHttpRequest->OnProcessRequestComplete().BindLambda(
		[WeakThis = TWeakObjectPtr(subsystem),_OnVerifyCodeCompleted]
		(FHttpRequestPtr _Request, const FHttpResponsePtr& _Response, bool _bWasSuccessful)
		{
			// 防炸补丁
			if(!WeakThis.IsValid())
				return ;
			// 基本上都是网络错误
			if(!_bWasSuccessful || !_Response.IsValid())
			{
				(void)_OnVerifyCodeCompleted.ExecuteIfBound(EVerifyCodeStatus::NetworkError);
				return;
			}
			// 解析响应
			TSharedPtr<FJsonObject> newJsonObject;
			const TSharedRef<TJsonReader<>> reader =
				TJsonReaderFactory<>::Create(_Response->GetContentAsString());
			// 卡脖子了
			if(!FJsonSerializer::Deserialize(reader, newJsonObject))
			{
				(void)_OnVerifyCodeCompleted.ExecuteIfBound(EVerifyCodeStatus::UnknownError);
				return;
			}
			const int32 responseCode = _Response->GetResponseCode();
			// 如果不是200，验证失败，重新登录
			if(responseCode != 200)
			{
				(void)_OnVerifyCodeCompleted.ExecuteIfBound(EVerifyCodeStatus::UnknownError);
				return ;
			}
			// 如果是200，验证成功，获取到token
			FPlayerTokenInfo playerTokenInfo;
			playerTokenInfo.UserId		= newJsonObject->GetStringField(TEXT("userId"));
			playerTokenInfo.PlayerToken	= newJsonObject->GetStringField(TEXT("playerToken"));
			playerTokenInfo.ExpiresAt		= newJsonObject->GetStringField(TEXT("expiresAt"));
			// 保存token
			WeakThis->SaveToken(playerTokenInfo);
			
			(void)_OnVerifyCodeCompleted.ExecuteIfBound(EVerifyCodeStatus::GetPlayerToken);
		});
	subsystem->GetPlayerTokenHttpRequest->ProcessRequest();
}

//---------------------------------------------------
// Purpose: 验证码验证冷却倒计时
//---------------------------------------------------
void UDevworksAuthSubsystem::StartVerifyCooldownTimer(int32 _Seconds, FOnVerifyCooldownTimer _OnVerifyCooldownTimer)
{
	UDevworksAuthSubsystem* subsystem =	Get();
	if (!subsystem)
		return;
	ClearVerifyCooldownTimer();

	int32 remainSeconds = _Seconds;
	(void)_OnVerifyCooldownTimer.ExecuteIfBound(remainSeconds);

	
	GEngine->GameViewport->GetWorld()->GetTimerManager().SetTimer(
		subsystem->VerifyCooldownTimerHandle,
		[WeakThis = TWeakObjectPtr(subsystem),_OnVerifyCooldownTimer, remainSeconds]() mutable
		{
			// 防炸补丁
			if(!WeakThis.IsValid())
				return ;

			remainSeconds--;
			(void)_OnVerifyCooldownTimer.ExecuteIfBound(remainSeconds);

			// 时间到 → 停止定时器
			if (remainSeconds <= 0)
			{
				WeakThis->ClearVerifyCooldownTimer();
			}
			
		},
		1.0f,
		true);
}


//---------------------------------------------------
// Purpose: 停止 验证码验证冷却倒计时
//---------------------------------------------------
void UDevworksAuthSubsystem::ClearVerifyCooldownTimer()
{
	UDevworksAuthSubsystem* subsystem = Get();
	if (!subsystem)
		return;
	GEngine->GameViewport->GetWorld()->GetTimerManager().ClearTimer(subsystem->VerifyCooldownTimerHandle);
	
}

//-----------------------------------------------
// Purpose: 保存token
//-----------------------------------------------
void UDevworksAuthSubsystem::SaveToken(FPlayerTokenInfo _PlayerTokenInfo) const
{
	FBufferArchive toBinary;
	toBinary << _PlayerTokenInfo;
	
	TArray<uint8> encryptedData;
	encryptedData.Append(toBinary.GetData(), toBinary.Num());


	constexpr uint8 key[32] = {
		'A','G','e','N','t','L','A','n','D','D','e','V','E','l','0','p',
		'e','R','W','0','R','k','S','F','O','r','U','n','R','e','A','L'
	};

	// 补齐到16的倍数（PKCS7）
	int32 padding = 16 - (encryptedData.Num() % 16);
	if (padding != 0 && padding != 16)
	{
		for (int32 i = 0; i < padding; i++)
			encryptedData.Add(static_cast<uint8>(padding)); // 每个填充字节存 padding 数
	}
	
	
	FAES::EncryptData(encryptedData.GetData(), encryptedData.Num(), key,32);
	
	FFileHelper::SaveArrayToFile(encryptedData, *PlayerTokenSaveFilePath);

	// 清理内存
	toBinary.FlushCache();
	toBinary.Empty();
}

//------------------------------------------------
// Purpose: 获取token
//------------------------------------------------
bool UDevworksAuthSubsystem::GetToken(FPlayerTokenInfo& _OutPlayerTokenInfo,int32 _HoursEarly)
{
	UDevworksAuthSubsystem* subsystem = Get();
	if (!subsystem)
		return false;
	
	
	// 如果文件不存在,返回false
	if(!IFileManager::Get().FileExists(*(subsystem->PlayerTokenSaveFilePath)))
		return false;
	// 读取文件
	TArray<uint8> binaryData;
	bool bIsLoaded =
		FFileHelper::LoadFileToArray(binaryData, *(subsystem->PlayerTokenSaveFilePath));

	// 文件无法读取
	if (!bIsLoaded)
		return false;
	
	

	constexpr uint8 key[32] = {
		'A','G','e','N','t','L','A','n','D','D','e','V','E','l','0','p',
		'e','R','W','0','R','k','S','F','O','r','U','n','R','e','A','L'
	};
	if (binaryData.Num() > 0)
	{
		uint8 padding = binaryData.Last();
		if (padding > 0 && padding <= 16 && padding <= binaryData.Num())
		{
			binaryData.RemoveAt(binaryData.Num() - padding, padding,EAllowShrinking::No);
		}
	}


	FAES::DecryptData(binaryData.GetData(), binaryData.Num(), key,32);

	

	FMemoryReader fromBinary(binaryData, true);
	fromBinary.Seek(0);
	FPlayerTokenInfo playerTokenInfo;
	fromBinary << playerTokenInfo;

	fromBinary.FlushCache();
	fromBinary.Close();

	// 检查是否过期
	FString expiresAt = playerTokenInfo.ExpiresAt;
	
	if(playerTokenInfo.ExpiresAt.IsEmpty())
		return false;

	FDateTime expireTime;
	// 无法解析时视为过期
	if (!FDateTime::ParseIso8601(*expiresAt, expireTime))
	{
		return false; 
	}

	// 提前6小时过期
	FDateTime adjustedExpireTime =
		expireTime - FTimespan::FromHours(_HoursEarly);
	
	FDateTime nowTime = FDateTime::UtcNow();

	// 过期了
	if(nowTime > adjustedExpireTime)
		return false;
	
	_OutPlayerTokenInfo = playerTokenInfo;
	return true;
}
