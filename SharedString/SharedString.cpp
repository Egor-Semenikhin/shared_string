#include <iostream>

#include <cstdint>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <string>
#include <cassert>
#include <atomic>

template <typename TChar>
class SharedString
{
private:
    class SharedData;
    class SharedStorage;

private:
    SharedData* Data;
    static const uint8_t EmptyBuffer[];

public:
    SharedString();
    SharedString(const TChar* Str);
    SharedString(const std::basic_string<TChar>& Str);
    SharedString(const SharedString& Str);
    SharedString(SharedString&& Str);

    template <size_t Size>
    SharedString(const TChar(&Str)[Size]);

    ~SharedString();

    SharedString& operator = (const SharedString& Str);
    SharedString& operator = (SharedString&& Str);
    SharedString& operator = (const std::basic_string<TChar>& Str);
    SharedString& operator = (const TChar* Str);
    SharedString& operator = (const std::basic_string_view<TChar>& Str);

    template <size_t Size>
    SharedString& operator = (const TChar(&Str)[Size]);

    const TChar* CStr() const;
    std::basic_string<TChar> Str() const;

private:
    static SharedStorage& GetSharedStorage();
    static SharedData* GetEmpty();
    static SharedData* CopyRef(SharedData* Data);
    static SharedData* GetData(const TChar* Str);

    void Clear();
};

template <typename TChar>
class SharedString<TChar>::SharedData final
{
private:
    std::atomic<uint32_t> RefCount;
    uint32_t Length;

public:
    SharedData(const TChar* Str, uint32_t StrLength);

    const TChar* GetBuffer() const;
    uint32_t GetLength() const;

    void IncRefCount();
    uint32_t DecRefCount();
};

template <typename TChar>
class SharedString<TChar>::SharedStorage final
{
private:
    std::unordered_map<std::basic_string_view<TChar>, std::unique_ptr<SharedData>> Storage;
    std::mutex Mutex;

public:
    SharedData* AddString(const TChar* Str, uint32_t Length);
    void RemoveString(SharedData* Data);
};

template <typename TChar>
alignas(alignof(SharedString<TChar>::SharedData))
const uint8_t SharedString<TChar>::EmptyBuffer[sizeof(SharedData) + sizeof(TChar)] = { 0 };

template <typename TChar>
SharedString<TChar>::SharedStorage& SharedString<TChar>::GetSharedStorage()
{
    static SharedStorage StaticSharedStorage;
    return StaticSharedStorage;
}

template <typename TChar>
SharedString<TChar>::SharedData* SharedString<TChar>::GetEmpty()
{
    return const_cast<SharedData*>(reinterpret_cast<const SharedData*>(EmptyBuffer));
}

template <typename TChar>
SharedString<TChar>::SharedData* SharedString<TChar>::CopyRef(SharedData* Data)
{
    Data->IncRefCounter();
    return Data;
}

template <typename TChar>
SharedString<TChar>::SharedData* SharedString<TChar>::GetData(const TChar* Str)
{
    const size_t Length = std::basic_string_view<TChar>(Str).size();
    return Length == 0 ? GetEmpty() : GetSharedStorage().AddString(Str, uint32_t(Length));
}

template <typename TChar>
SharedString<TChar>::SharedString()
    : Data (GetEmpty())
{
}

template <typename TChar>
SharedString<TChar>::SharedString(const TChar* Str)
    : Data (GetData(Str))
{
}

template <typename TChar>
SharedString<TChar>::SharedString(const std::basic_string<TChar>& Str)
    : Data(Str.size() == 0 ? GetEmpty() : GetSharedStorage().AddString(Str.c_str(), uint32_t(Str.size())))
{
}

template <typename TChar>
SharedString<TChar>::SharedString(const SharedString& Str)
    : Data (Str.Data == GetEmpty() ? GetEmpty() : CopyRef(Str.Data))
{
}

template <typename TChar>
SharedString<TChar>::SharedString(SharedString&& Str)
    : Data (Str.Data)
{
    Str.Data = GetEmpty();
}

template <typename TChar>
template <size_t Size>
SharedString<TChar>::SharedString(const TChar(&Str)[Size])
    : Data(Size == 0 ? GetEmpty() : GetSharedStorage().AddString(Str, uint32_t(Size - 1)))
{
}

template <typename TChar>
void SharedString<TChar>::Clear()
{
    if (Data != GetEmpty())
    {
        GetSharedStorage().RemoveString(Data);
    }
}

template <typename TChar>
SharedString<TChar>::~SharedString()
{
    Clear();
}

template <typename TChar>
SharedString<TChar>& SharedString<TChar>::operator = (const SharedString<TChar>& Str)
{
    if (Str.Data != Data)
    {
        Clear();
        Data = Str.Data == GetEmpty() ? GetEmpty() : CopyRef(Str.Data);
    }
    return *this;
}

template <typename TChar>
SharedString<TChar>& SharedString<TChar>::operator = (SharedString&& Str)
{
    if (Str.Data != Data)
    {
        Clear();
        Data = Str.Data;
        Str.Data = GetEmpty();
    }
    return *this;
}

template <typename TChar>
SharedString<TChar>& SharedString<TChar>::operator = (const std::basic_string_view<TChar>& Str)
{
    Clear();
    Data = Str.size() == 0 ? GetEmpty() : GetSharedStorage().AddString(Str.data(), uint32_t(Str.size()));
    return *this;
}

template <typename TChar>
SharedString<TChar>& SharedString<TChar>::operator = (const std::basic_string<TChar>& Str)
{
    return operator = (std::basic_string_view<TChar>(Str.c_str(), Str.size()));
}

template <typename TChar>
SharedString<TChar>& SharedString<TChar>::operator = (const TChar* Str)
{
    return operator = (std::basic_string_view<TChar>(Str));
}

template <typename TChar>
template <size_t Size>
SharedString<TChar>& SharedString<TChar>::operator = (const TChar(&Str)[Size])
{
    return operator = (std::basic_string_view<TChar>(Str, Size));
}

template <typename TChar>
const TChar* SharedString<TChar>::CStr() const
{
    return Data->GetBuffer();
}

template <typename TChar>
std::basic_string<TChar> SharedString<TChar>::Str() const
{
    return std::basic_string<TChar>(Data->GetBuffer(), Data->GetLength());
}

template <typename TChar>
SharedString<TChar>::SharedData* SharedString<TChar>::SharedStorage::AddString(const TChar* Str, uint32_t Length)
{
    const std::basic_string_view<TChar> StringView(Str, Length);
    const std::lock_guard<std::mutex> guard(Mutex);
    const auto Iter = Storage.find(StringView);

    if (Iter != Storage.end())
    {
        SharedData* const Result = Iter->second.get();
        Result->IncRefCount();
        return Result;
    }

    const size_t BufferSize = sizeof(SharedData) + (Length + 1) * sizeof(TChar);
    SharedData* const Result = new (new uint8_t[BufferSize]) SharedData(Str, Length);

    Storage.emplace(StringView, Result);

    return Result;
}

template <typename TChar>
void SharedString<TChar>::SharedStorage::RemoveString(SharedData* Data)
{
    if (Data->DecRefCount() == 0)
    {
        const std::lock_guard<std::mutex> guard(Mutex);
        Storage.erase(Storage.find(std::basic_string_view<TChar>(Data->GetBuffer(), Data->GetLength())));
    }
}

template <typename TChar>
SharedString<TChar>::SharedData::SharedData(const TChar* Str, uint32_t StrLength)
    : RefCount (1)
    , Length (StrLength)
{
    TChar* const Buffer = const_cast<TChar*>(GetBuffer());
    memcpy(Buffer, Str, StrLength * sizeof(TChar));
    Buffer[StrLength] = TChar(0);
}

template <typename TChar>
const TChar* SharedString<TChar>::SharedData::GetBuffer() const
{
    const void* const Result = reinterpret_cast<const uint8_t*>(this) + sizeof(SharedData);
    return static_cast<const TChar*>(Result);
}

template <typename TChar>
uint32_t SharedString<TChar>::SharedData::GetLength() const
{
    return Length;
}

template <typename TChar>
void SharedString<TChar>::SharedData::IncRefCount()
{
    ++RefCount;
}

template <typename TChar>
uint32_t SharedString<TChar>::SharedData::DecRefCount()
{
    return --RefCount;
}

int main()
{
    SharedString<char> str = "str";
    SharedString<char> str1 = "str";
    SharedString<char> str2 = std::move(str1);
    SharedString<char> str3 = "abcd";
    str2 = "12345";
}
