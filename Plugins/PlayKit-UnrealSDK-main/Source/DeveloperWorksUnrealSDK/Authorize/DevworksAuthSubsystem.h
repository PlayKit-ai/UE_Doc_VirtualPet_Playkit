// Copyright Agentland. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DevworksAuthSubsystem.generated.h"


class IHttpRequest;


//////////////// Login  ////////////////
UENUM(Blueprintable)
enum class ERequestCodeStatus : uint8
{
	Success			UMETA(DisplayName = "请求成功"),
	InvalidEmail	UMETA(DisplayName = "邮箱格式错误"),
	InvalidPhone	UMETA(DisplayName = "手机号格式错误"),
	MissingParam	UMETA(DisplayName = "缺少参数"),
	NetworkError	UMETA(DisplayName = "网络错误"),
	TooMany			UMETA(DisplayName = "登录次数过于频繁，请稍后再试"),
	UnknownError	UMETA(DisplayName = "未知错误")
};

UENUM(Blueprintable)
enum class ELoginType : uint8
{
	Email			UMETA(DisplayName = "邮箱"),
	Phone			UMETA(DisplayName = "手机号")
};

UENUM(Blueprintable)
enum class EVerifyCodeStatus : uint8
{
	GetPlayerToken	UMETA(DisplayName = "已获取玩家令牌"),
	Success			UMETA(DisplayName = "验证码验证成功"),
	InvalidCode		UMETA(DisplayName = "验证码无效"),
	Expired			UMETA(DisplayName = "验证码已过期"),
	NetworkError	UMETA(DisplayName = "网络错误"),
	UnknownError	UMETA(DisplayName = "未知错误"),
	TooMany			UMETA(DisplayName = "验证次数过于频繁，请稍后再试")
};


// 请求验证码完成委托
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnRequestCodeCompleted,ERequestCodeStatus,Status);

// 验证冷却倒计时委托
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnVerifyCooldownTimer, int32, RemainingSeconds);

// 验证-验证码完成委托
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnVerifyCodeCompleted,EVerifyCodeStatus,Status);


//////////////// 玩家客户端   ////////////////
UENUM(Blueprintable)
enum class EUserRole: uint8
{
	Player			UMETA(DisplayName = "玩家"),
	Developer		UMETA(DisplayName = "开发者")
};

USTRUCT(BlueprintType)
struct FPlayerTokenInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString		UserId;
	UPROPERTY(BlueprintReadOnly)
	FString		PlayerToken;
	UPROPERTY(BlueprintReadOnly)
	FString		ExpiresAt;

	
public://------ 功能 ------//
	friend FArchive& operator<<(FArchive& _Ar,FPlayerTokenInfo& _UserTokenInfo)
	{
		_Ar << _UserTokenInfo.UserId;
		_Ar << _UserTokenInfo.PlayerToken;
		_Ar << _UserTokenInfo.ExpiresAt;
		return _Ar;
	}

	
};



USTRUCT(BlueprintType)
struct FUserClientInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString		UserId;
	UPROPERTY(BlueprintReadOnly)
	FString		GlobalToken;
	UPROPERTY(BlueprintReadOnly)
	FString		UserName;
	UPROPERTY(BlueprintReadOnly)
	EUserRole	Role;
	UPROPERTY(BlueprintReadOnly)
	UTexture2D*	Avatar;
};

// 需重新登录委托
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNeedReLogin);

// 保存客户端信息完成委托
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGetClientInfoCompleted,FUserClientInfo,UserClientInfo);

///////////////////////////////////////////


UCLASS(BlueprintType)
class DEVELOPERWORKSUNREALSDK_API UDevworksAuthSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()


public:
	virtual void BeginDestroy() override;

public://------ 自获取 ------//
	//UFUNCTION(BlueprintCallable,BlueprintPure, Category="DeveloperWorks|登录")
	static UDevworksAuthSubsystem* Get();


public://------ 功能 ------//

	//////////////// Login  ////////////////
	UFUNCTION(BlueprintCallable, Category="DeveloperWorks|登录",
		meta=(DisplayName="请求验证码"))//http
	static void RequestCode(FString _Address, ELoginType _LoginType, FOnRequestCodeCompleted _OnRequestCodeCompleted); 

	UFUNCTION(BlueprintCallable, Category="DeveloperWorks|登录",
			meta=(DisplayName="验证-验证码"))//http
	static void VerifyCode(FString _Code,
		FOnVerifyCodeCompleted _OnVerifyCodeCompleted);

	UFUNCTION()//http
	static void GetPlayerToken(const FString& _GlobalToken,FOnVerifyCodeCompleted _OnVerifyCodeCompleted);
	
	UFUNCTION(BlueprintCallable, Category="DeveloperWorks|登录",
		meta=(DisplayName="验证码冷却倒计时"))
	static void StartVerifyCooldownTimer(int32 _Seconds,FOnVerifyCooldownTimer _OnVerifyCooldownTimer);

	UFUNCTION(BlueprintCallable, Category="DeveloperWorks|登录",
		meta=(DisplayName="清除验证码冷却倒计时"))
	static void ClearVerifyCooldownTimer();
	//////////////// Client  ////////////////
	void SaveToken(FPlayerTokenInfo _PlayerTokenInfo) const;

	UFUNCTION(BlueprintCallable, Category="DeveloperWorks|登录",
		meta=(DisplayName="获取PlayerToken信息"))
	static bool GetToken(FPlayerTokenInfo& _OutPlayerTokenInfo,int32 _HoursEarly = 6);

public://------ 组件 ------//
	//////////////// Login  ////////////////

	// 验证码冷却倒计时
	FTimerHandle VerifyCooldownTimerHandle;

	// 请求验证码的HttpRequest
	TSharedPtr<IHttpRequest> RequestCodeHttpRequest;

	// 验证-验证码的HttpRequest
	TSharedPtr<IHttpRequest> VerifyCodeHttpRequest;

	// 获取PlayerToken的HttpRequest
	TSharedPtr<IHttpRequest> GetPlayerTokenHttpRequest;

	// 获取客户端信息的回调委托
	UPROPERTY(BlueprintAssignable, Category="DeveloperWorks|登录")
	FOnGetClientInfoCompleted OnGetClientInfoCompleted;

	//////////////// Client  ////////////////
	// 需重新登录的委托
	UPROPERTY(BlueprintAssignable, Category="DeveloperWorks|登录")
	FOnNeedReLogin OnNeedReLogin;

public://------ 参数 ------//
	//////////////// Login  ////////////////
	// 请求验证码的会话ID
	FString RequestCodeSessionId;
	
	UPROPERTY(BlueprintReadOnly, Category="DeveloperWorks|URLs")
	FString BaseURL = FString("https://developerworks.agentlandlab.com");

	//////////////// Client  ////////////////
	const FString PlayerTokenSaveFilePath = FPaths::ProjectSavedDir() / TEXT("DeveloperWorks/PlayerToken.dat");
	FUserClientInfo UserClientInfo;

};
