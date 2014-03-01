#include "stdafx.h"
#include "ProtoJSON.h"

#include <jansson.h>

#include <memory>

#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>

#pragma warning( disable : 4244 )

using std::unique_ptr;
using std::shared_ptr;

using namespace google::protobuf;

namespace ProtoJSON
{
	void fromJSON(google::protobuf::Message& msg, const std::string& source)
	{
		unique_ptr<json_error_t> error;
		unique_ptr<json_t, Internal::jansson_deleter> rootElement(json_loads(source.c_str(), 0, error.get()));

		if (!rootElement)
			throw new ProtoJSONException("JSON Load Failed: " + *error->text);

		if (!json_is_object(rootElement.get()))
			throw new ProtoJSONException("Load failed on singular object as root element was not a valid object.");

		Internal::ParseObject(msg, rootElement.get());
	}

	std::string asJSON(const MsgProto& msg, size_t flags /*= HumanReadable*/, bool stripExtensionName /*= false*/)
	{
		std::string data;
		toJSON(msg, data, flags, stripExtensionName);
		return data;
	}

	void toJSON(const google::protobuf::Message& msg, std::string& json, size_t flags /*= 0*/, bool stripExtensionName /*= false*/)
	{
		unique_ptr<json_t, Internal::jansson_deleter> rootElement(Internal::ConvertObject(msg, stripExtensionName));

		json_dump_callback(rootElement.get(), Internal::json_dump_std_string, &json, flags);
	}

	namespace Internal
	{
#pragma region Base64
		const std::string base64_chars =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz"
			"0123456789+/";

		static inline bool is_base64(unsigned char c) {
			return (isalnum(c) || (c == '+') || (c == '/'));
		}

		std::string base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) {
			std::string ret;
			int i = 0;
			int j = 0;
			unsigned char char_array_3[3];
			unsigned char char_array_4[4];

			while (in_len--) {
				char_array_3[i++] = *(bytes_to_encode++);
				if (i == 3) {
					char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
					char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
					char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
					char_array_4[3] = char_array_3[2] & 0x3f;

					for (i = 0; (i < 4); i++)
						ret += base64_chars[char_array_4[i]];
					i = 0;
				}
			}

			if (i)
			{
				for (j = i; j < 3; j++)
					char_array_3[j] = '\0';

				char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
				char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
				char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
				char_array_4[3] = char_array_3[2] & 0x3f;

				for (j = 0; (j < i + 1); j++)
					ret += base64_chars[char_array_4[j]];

				while ((i++ < 3))
					ret += '=';

			}

			return ret;

		}

		std::string base64_decode(std::string const& encoded_string)
		{
			int in_len = encoded_string.size();
			int i = 0;
			int j = 0;
			int in_ = 0;
			unsigned char char_array_4[4], char_array_3[3];
			std::string ret;

			while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
				char_array_4[i++] = encoded_string[in_]; in_++;
				if (i == 4) 
				{
					for (i = 0; i < 4; i++)
						char_array_4[i] = base64_chars.find(char_array_4[i]);

					char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
					char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
					char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

					for (i = 0; (i < 3); i++)
						ret += char_array_3[i];
					i = 0;
				}
			}

			if (i) {
				for (j = i; j < 4; j++)
					char_array_4[j] = 0;

				for (j = 0; j < 4; j++)
					char_array_4[j] = base64_chars.find(char_array_4[j]);

				char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
				char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
				char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

				for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
			}

			return ret;
		}
#pragma endregion

		int json_dump_std_string(const char *buf, size_t size, void *data)
		{
			std::string *s = (std::string *) data;
			s->append(buf, size);
			return 0;
		}

		void ParseObject(google::protobuf::Message& msg, json_t* jsonElement)
		{
			const Descriptor* desc = msg.GetDescriptor();
			const Reflection* reflect = msg.GetReflection();

			if (!desc || !reflect)
				throw new ProtoJSONException("Descriptor or Reflection was null, cannot parse JSON into message.");

			const char *key;
			json_t* currentElement;

			json_object_foreach(jsonElement, key, currentElement)
			{
				const FieldDescriptor* field = desc->FindFieldByName(key);

				if (!field)
					field = reflect->FindKnownExtensionByName(key);

				// Will handle unknown fields when I actually understand how to handle them.
				if (!field)
				{
					UnknownFieldSet* ufs = reflect->MutableUnknownFields(&msg);

					// TODO: Handle Unknown Fields into Unknown Field on Message.
					//throw new ProtoJSONException("Unknown Field: " + string(key));
					continue;
				}

				if (field->is_repeated())
				{
					if (!json_is_array(currentElement))
						throw new ProtoJSONException("Tried to parse a repeated field that was not an array.");

					size_t index;
					json_t* currentArrayElement;

					json_array_foreach(currentElement, index, currentArrayElement)
					{
						ParseElementToField(msg, field, currentArrayElement);
					}
				}
				else
					ParseElementToField(msg, field, currentElement);
			}
		}

		void ParseElementToField(MsgProto& msg, const google::protobuf::FieldDescriptor* field, json_t* jsonElement)
		{
			const Reflection* reflect = msg.GetReflection();
			const bool repeated = field->is_repeated();

			switch (field->cpp_type())
			{
			case FieldDescriptor::CPPTYPE_DOUBLE:
			{
				if (repeated)
					reflect->AddDouble(&msg, field, json_number_value(jsonElement));
				else
					reflect->SetDouble(&msg, field, json_number_value(jsonElement));

				break;
			}

			case FieldDescriptor::CPPTYPE_FLOAT:
			{
				if (repeated)
					reflect->AddFloat(&msg, field, static_cast<float>(json_number_value(jsonElement)));
				else
					reflect->SetFloat(&msg, field, static_cast<float>(json_number_value(jsonElement)));

				break;
			}

			case FieldDescriptor::CPPTYPE_INT64:
			{
				if (repeated)
					reflect->AddInt64(&msg, field, json_integer_value(jsonElement));
				else
					reflect->SetInt64(&msg, field, json_integer_value(jsonElement));

				break;
			}
			case FieldDescriptor::CPPTYPE_UINT64:
			{
				if (repeated)
					reflect->AddUInt64(&msg, field, json_integer_value(jsonElement));
				else
					reflect->SetUInt64(&msg, field, json_integer_value(jsonElement));

				break;
			}
			case FieldDescriptor::CPPTYPE_INT32:
			{
				if (repeated)
					reflect->AddInt32(&msg, field, json_integer_value(jsonElement));
				else
					reflect->SetInt32(&msg, field, json_integer_value(jsonElement));

				break;
			}
			case FieldDescriptor::CPPTYPE_UINT32:
			{
				if (repeated)
					reflect->AddUInt32(&msg, field, json_integer_value(jsonElement));
				else
					reflect->SetUInt32(&msg, field, json_integer_value(jsonElement));

				break;
			}
			case FieldDescriptor::CPPTYPE_BOOL:
			{
				json_error_t* err = nullptr;

				int value;																	
				if (!json_unpack_ex(jsonElement, err, JSON_STRICT, "b", &value))
				{
					if (repeated)
						reflect->AddBool(&msg, field, value != 0);
					else
						reflect->SetBool(&msg, field, value != 0);
				}
				else
					throw new ProtoJSONException("Failed to unpack boolean: " + *err->text);

				break;
			}

			case FieldDescriptor::CPPTYPE_STRING: 
			{
				if (!json_is_string(jsonElement))
					throw new ProtoJSONException("Attempted to set field that requires string to field that was not a string.");

				const string val = field->type() == FieldDescriptor::TYPE_BYTES ? base64_decode(json_string_value(jsonElement)) : json_string_value(jsonElement);

				if (repeated)
					reflect->AddString(&msg, field, val);
				else
					reflect->SetString(&msg, field, val);

				break;
			}
			case FieldDescriptor::CPPTYPE_MESSAGE: 
			{
				Message *mf = (repeated) ? reflect->AddMessage(&msg, field) : reflect->MutableMessage(&msg, field);
				ParseObject(*mf, jsonElement);
				break;
			}
			case FieldDescriptor::CPPTYPE_ENUM: 
			{
				const EnumDescriptor *ed = field->enum_type();
				const EnumValueDescriptor *ev = 0;
				if (json_is_integer(jsonElement)) 
					ev = ed->FindValueByNumber(json_integer_value(jsonElement));
				else if (json_is_string(jsonElement)) 
					ev = ed->FindValueByName(json_string_value(jsonElement));

				else
					throw ProtoJSONException("Unable to parse enum that is not an integer or string.");
				if (!ev)
					throw ProtoJSONException("Enum value not found.");

				if (repeated)
					reflect->AddEnum(&msg, field, ev);
				else
					reflect->SetEnum(&msg, field, ev);

				break;
			}
			default:
				break;
			}
		}

		json_t* ConvertObject(const MsgProto& msg, bool stripExtensionName)
		{
			const Descriptor* desc = msg.GetDescriptor();
			const Reflection* reflect = msg.GetReflection();

			unique_ptr<json_t> rootElement(json_object());

			std::vector<const FieldDescriptor *> fields;
			reflect->ListFields(msg, &fields);

			for (const FieldDescriptor* field : fields)
			{
				json_t* jsonField = nullptr;

				if (field->is_repeated())
				{
					size_t count = reflect->FieldSize(msg, field);
					if (!count) continue;

					unique_ptr<json_t> rfArray(json_array());
					for (size_t j = 0; j < count; j++)
						json_array_append_new(rfArray.get(), ConvertFieldToElement(msg, field, j));
					jsonField = rfArray.release();
				}
				else if (reflect->HasField(msg, field))
				{
					jsonField = ConvertFieldToElement(msg, field, 0);
				}
				else continue;

				const std::string &name = ((field->is_extension() && !stripExtensionName) ? field->full_name() : field->name());
				json_object_set_new(rootElement.get(), name.c_str(), jsonField);
			}

			return rootElement.release();
		}

		json_t* ConvertFieldToElement(const MsgProto& msg, const google::protobuf::FieldDescriptor* field, size_t index)
		{
			const Reflection* reflect = msg.GetReflection();
			const bool repeated = field->is_repeated();

			json_t *jf = 0;

			switch (field->cpp_type())
			{
			case FieldDescriptor::CPPTYPE_DOUBLE:
			{
				const double value = (repeated) ? reflect->GetRepeatedDouble(msg, field, index) : reflect->GetDouble(msg, field);
				jf = json_real(value);
				break;
			}

			case FieldDescriptor::CPPTYPE_FLOAT:
			{
				const double value = (repeated) ? reflect->GetRepeatedFloat(msg, field, index) : reflect->GetFloat(msg, field);
				jf = json_real(value);
				break;
			}

			case FieldDescriptor::CPPTYPE_INT64:
			{
				const json_int_t value = (repeated) ? reflect->GetRepeatedInt64(msg, field, index) : reflect->GetInt64(msg, field);
				jf = json_integer(value);
				break;
			}
			case FieldDescriptor::CPPTYPE_UINT64:
			{
				const json_int_t value = (repeated) ? reflect->GetRepeatedUInt64(msg, field, index) : reflect->GetUInt64(msg, field);
				jf = json_integer(value);
				break;
			}
			case FieldDescriptor::CPPTYPE_INT32:
			{
				const json_int_t value = (repeated) ? reflect->GetRepeatedInt32(msg, field, index) : reflect->GetInt32(msg, field);
				jf = json_integer(value);
				break;
			}
			case FieldDescriptor::CPPTYPE_UINT32:
			{
				const json_int_t value = (repeated) ? reflect->GetRepeatedUInt32(msg, field, index) : reflect->GetUInt32(msg, field);
				jf = json_integer(value);
				break;
			}
			case FieldDescriptor::CPPTYPE_BOOL:
			{
				const bool value = (repeated) ? reflect->GetRepeatedBool(msg, field, index) : reflect->GetBool(msg, field);
				jf = json_boolean(value);
				break;
			}

			case FieldDescriptor::CPPTYPE_STRING:
			{
				std::string scratch;
				const std::string &value = (repeated) ?
					reflect->GetRepeatedStringReference(msg, field, index, &scratch) : reflect->GetStringReference(msg, field, &scratch);
				if (field->type() == FieldDescriptor::TYPE_BYTES)
					jf = json_string(base64_encode(reinterpret_cast<const unsigned char*>(value.c_str()), value.length()).c_str());
				else
					jf = json_string(value.c_str());
				break;
			}
			case FieldDescriptor::CPPTYPE_MESSAGE:
			{
				const Message& mf = (repeated) ? reflect->GetRepeatedMessage(msg, field, index) : reflect->GetMessage(msg, field);
				jf = ConvertObject(mf);
				break;
			}
			case FieldDescriptor::CPPTYPE_ENUM:
			{
				const EnumValueDescriptor* ef = (repeated) ? reflect->GetRepeatedEnum(msg, field, index) : reflect->GetEnum(msg, field);

				jf = json_integer(ef->number());
				break;
			}
			default:
				break;
			}

			return jf;
		}

	}

	ProtoJSONException::ProtoJSONException(std::string reason) : mReason(reason) { }

	const char * ProtoJSONException::what() const throw ()
	{
		return mReason.c_str();
	}
}
