#pragma once

#include <jansson.h>

#include <string>
#include <vector>


#include <memory>
#include <exception>

#include <type_traits>

namespace google 
{
	namespace protobuf 
	{
		class Message;
		class FieldDescriptor;
	}
}

namespace ProtoJSON
{
	typedef google::protobuf::Message MsgProto;

	static size_t HumanReadable = JSON_INDENT(4) | JSON_PRESERVE_ORDER;
	static size_t FormatCompact = JSON_COMPACT;

#pragma region JSON -> PB

	////////////////////////////////////////////////////////////////////////////////////////////////////
	/// <summary>
	/// 	Sets the appropriate fields in the message by parsing the JSON string.
	/// </summary>
	///
	/// <param name="msg">   	[in,out] The message. </param>
	/// <param name="source">	Source for the. </param>
	////////////////////////////////////////////////////////////////////////////////////////////////////
	void fromJSON(MsgProto& msg, const std::string& source);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	/// <summary>
	/// 	Populates the messages vector by parsing each entry in the JSON array as a message of the
	/// 	type specified by T.
	/// </summary>
	///
	/// <exception cref="ProtoJSONException">	Thrown when a prototype JSON error condition occurs. </exception>
	///
	/// <typeparam name="T">	Generic type parameter. </typeparam>
	/// <param name="messages">	[in,out] The messages. </param>
	/// <param name="source">  	Source for the. </param>
	///
	/// <returns>
	/// 	A void>::type.
	/// </returns>
	////////////////////////////////////////////////////////////////////////////////////////////////////
	template <typename T>
	typename std::enable_if<std::is_base_of<MsgProto, T>::value, void>::type
	fromJSON(std::vector<T>& messages, const std::string& source)
	{
		std::unique_ptr<json_error_t> error;
		std::unique_ptr<json_t, Internal::jansson_deleter> rootElement(json_loads(source.c_str(), 0, error.get()));

		if (!rootElement)
			throw new ProtoJSONException("JSON Load Failed: " + *error->text);

		if (!json_is_array(rootElement.get()))
			throw new ProtoJSONException("Load failed on vector of objects as the root element was not a valid array.");

		size_t index;
		json_t* currentElement;

		json_array_foreach(rootElement.get(), index, currentElement)
		{
			if (!json_is_object(currentElement))
				throw new ProtoJSONException("Load failed on object in array at index " + std::to_string(index) + " check to make sure that its an object.");

			T newMessage;

			Internal::ParseObject(newMessage, currentElement);

			messages.push_back(newMessage);
		}
	}
#pragma endregion

#pragma region PB -> JSON
	std::string asJSON(const MsgProto& msg, size_t flags = HumanReadable, bool stripExtensionName = false);

	template <typename T>
	typename std::enable_if<std::is_base_of<MsgProto, T>::value, std::string>::type
	asJSON(std::vector<T>& messages, size_t flags = HumanReadable, bool stripExtensionName = false)
	{
		std::string data;
		toJSON(messages, data, flags, stripExtensionName);
		return data;
	}

	void toJSON(const MsgProto& msg, std::string& json, size_t flags = HumanReadable, bool stripExtensionName = false);

	template <typename T>
	typename std::enable_if<std::is_base_of<MsgProto, T>::value, void>::type
	toJSON(std::vector<T>& messages, std::string& json, size_t flags = HumanReadable, bool stripExtensionName = false)
	{
		std::unique_ptr<json_t, Internal::jansson_deleter> rootElement(json_array());

		for (auto currentMessage : messages)
			json_array_append_new(rootElement.get(), Internal::ConvertObject(currentMessage, stripExtensionName));

		json_dump_callback(rootElement.get(), Internal::json_dump_std_string, &json, flags);
	}
#pragma endregion


	class ProtoJSONException : public std::exception
	{
	public:
		ProtoJSONException(std::string reason);

		virtual const char *what() const throw ();
	private:
		std::string mReason;
	};

	namespace Internal
	{
		struct jansson_deleter
		{
			void operator ()(json_t *ref)
			{
				json_decref(ref);
			}
		};

		int json_dump_std_string(const char *buf, size_t size, void *data);

		void ParseObject(MsgProto& msg, json_t* jsonElement);
		void ParseElementToField(MsgProto& msg, const google::protobuf::FieldDescriptor* field, json_t* jsonElement);

		json_t* ConvertObject(const MsgProto& msg, bool stripExtensionName = false);
		json_t* ConvertFieldToElement(const MsgProto& msg, const google::protobuf::FieldDescriptor* field, size_t index);
	}
}