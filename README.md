ProtoJSON
=========

ProtoJSON is a wrapper around jansson to allow dumping protocol buffers to JSON, it has allowances to handle extensions (And can shorten extensions if you use it to serialize to something like a REST API)


Inspired by https://github.com/shramov/json2pb but the code needed some fixing and cleanup and I didn't feel like doing it on someone elses codebase, and ProtoJSON also handles vectors and json arrays.
