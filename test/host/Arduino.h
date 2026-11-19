#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const char* s, unsigned int n) : s_(s, n) {}
  String(char c) : s_(1, c) {}
  String(unsigned char v) : s_(std::to_string(static_cast<unsigned>(v))) {}
  String(unsigned int v) : s_(std::to_string(v)) {}
  String(int v) : s_(std::to_string(v)) {}
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  String& operator+=(const char* o) { s_ += o; return *this; }
  String& operator+=(char c) { s_ += c; return *this; }
  String operator+(const String& o) const { String r(*this); r += o; return r; }
  bool operator==(const char* o) const { return s_ == o; }
  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator!=(const char* o) const { return s_ != o; }
  bool startsWith(const String& p) const { return s_.rfind(p.s_, 0) == 0; }
  String substring(size_t from) const { return String(s_.substr(from).c_str()); }
  size_t length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  const char* c_str() const { return s_.c_str(); }
  void reserve(size_t n) { s_.reserve(n); }
  char operator[](size_t i) const { return s_[i]; }
  std::string str() const { return s_; }
 private:
  std::string s_;
};
