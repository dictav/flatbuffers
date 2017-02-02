/*
 * Copyright 2014 Google Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// independent from idl_parser, since this code is not needed for most clients

#include <string>

#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/idl.h"
#include "flatbuffers/util.h"
#include "flatbuffers/code_generators.h"

#ifdef _WIN32
#include <direct.h>
#define PATH_SEPARATOR "\\"
#define mkdir(n, m) _mkdir(n)
#else
#include <sys/stat.h>
#define PATH_SEPARATOR "/"
#endif

namespace flatbuffers {

namespace go {
class GoGenerator : public BaseGenerator {
 public:
  GoGenerator(const Parser &parser, const std::string &path,
        const std::string &file_name)
  : BaseGenerator(parser, path, file_name, "" /* not used*/,
          "" /* not used */){};
  bool generate() override {
    if (!checkMultipleBaseName()) {
      throw std::logic_error("Multiple base names can not be mixed in Go generator.");
    }
    for (auto it = parser_.enums_.vec.begin(); it != parser_.enums_.vec.end();
       ++it) {
      cur_name_space_ = (*it)->defined_namespace;
      additional_import_namespaces_.clear();
      std::string enumcode;
      GenEnum(**it, &enumcode);
      if (!SaveType(**it, enumcode, false)) return false;
    }
    
    for (auto it = parser_.structs_.vec.begin();
       it != parser_.structs_.vec.end(); ++it) {
      cur_name_space_ = (*it)->defined_namespace;
      additional_import_namespaces_.clear();
      std::string declcode;
      GenStruct(**it, &declcode);
      if (!SaveType(**it, declcode, true)) return false;
    }
    
    return true;
  }
    
 private:
  std::vector<Namespace> additional_import_namespaces_;
  const Namespace *cur_name_space_;
  const Namespace *CurrentNameSpace() const override {
    return cur_name_space_;
  }

  bool checkMultipleBaseName() {

    std::string base_name = "";
    for (auto it = parser_.enums_.vec.begin(); it != parser_.enums_.vec.end(); ++it) {
      auto base = (*it)->defined_namespace->base;
      auto go_it = base.find("Go");

      if (go_it == base.end() || go_it->second.length() == 0) {
        continue;
      }

      auto bn = go_it->second;
      if (base_name.length() == 0) {
        base_name = bn;
        continue;
      }

      if (bn != base_name) {
        return false;
      }
    }
    return true;
  }

  std::string GenTypeName(const Definition &def) {
    auto v1 = def.defined_namespace->components;
    if (v1.size() == 0) {
      return def.name;
    }

    auto v2 = cur_name_space_->components;
    if (v1.size() == v2.size() && std::equal(v1.cbegin(), v1.cend(), v2.cbegin())) {
      return def.name;
    }

    bool includes_namespace = false;
    for (auto n: additional_import_namespaces_) {
      auto v3 = n.components;
      if (v1.size() == v3.size() && std::equal(v1.cbegin(), v1.cend(), v3.cbegin())) {
        includes_namespace = true;
        break;
      }
    }
    if (!includes_namespace) {
      additional_import_namespaces_.push_back(*def.defined_namespace);
    }
    return v1.back() + "." + def.name;
  }

  std::string GenPackagePath(const Namespace &ns) {
    std::string name;
    auto base = ns.base.find("Go");
    auto cur_base = cur_name_space_->base.find("Go");
    if (base == ns.base.end() && cur_base == cur_name_space_->base.end()) {
      return GenRelativeImportPath(ns);
    }

    if (base == ns.base.end() && cur_base != cur_name_space_->base.end()) {
      base = cur_base;
    }

    for (unsigned long i = 0; i < ns.components.size(); i++) {
      if (i > 0) {
        name += kPosixPathSeparator;
      }
      name += ns.components[i];
    }

    auto path = base->second + "/" + path_ + name;
#ifdef _WIN32
    replace(path.begin(), path.end(), kPathSeparator, kPosixPathSeparator);
#endif
    return path;

  }

  std::string GenRelativeImportPath(const Namespace &ns) {
    auto v1 = cur_name_space_->components;
    auto v2 = ns.components;
    if (v1.size() == 0 || v2.size() == 0) {
      return "";
    }

    auto slen = v2.size();
    if (slen > v1.size()) {
      slen = v1.size();
    }

    unsigned long idx = -1;
    for (unsigned long i = 0; i < slen; i++) {
      if (v1[i] == v2[i]) {
        idx = i;
      }
    }

    std::string path = "";
    for (unsigned long i = 0; i < v1.size()-(idx+1); i++) {
      path += "../";
    }

    if (path.length() > 0 && slen == idx+1) {
      return path.substr(0, path.length()-1);
    }

    if (path.length() == 0 ) {
      path = "./";
    }

    for (auto i = idx + 1; i < v2.size(); i++) {
      path += v2[i] + "/";
    }

    return path.substr(0, path.length()-1);
  }

  // Begin by declaring namespace and imports.
  void BeginFile(const std::string name_space_name, const bool needs_imports,
           std::string *code_ptr) {
    std::string &code = *code_ptr;
    code = code + "// " + FlatBuffersGeneratedWarning();
    code += "package " + name_space_name + "\n\n";
    if (!needs_imports && additional_import_namespaces_.size() == 0) {
      return;
    }
    code += "import (\n";
    if (needs_imports) {
      code += "\tflatbuffers \"github.com/google/flatbuffers/go\"\n";
    }
    for (auto &n: additional_import_namespaces_) {
      auto pkg = LastNamespacePart(n);
      auto path = GenPackagePath(n);
      code += "\t" + pkg + " \"" + path + "\"\n";
    }
    code += ")\n\n";
  }
  
  // Save out the generated code for a Go Table type.
  bool SaveType(const Definition &def, const std::string &classcode,
          bool needs_imports) {
    if (!classcode.length()) return true;
    
    std::string code = "";
    BeginFile(LastNamespacePart(*def.defined_namespace), needs_imports, &code);
    code += classcode;
    std::string filename =
    NamespaceDir(*def.defined_namespace) + def.name + ".go";
    return SaveFile(filename.c_str(), code, false);
  }


  // Most field accessors need to retrieve and test the field offset first,
  // this is the prefix code for that.
  std::string OffsetPrefix(const FieldDef &field) {
    return "{\n\to := flatbuffers.UOffsetT(rcv._tab.Offset(" +
           NumToString(field.value.offset) +
           "))\n\tif o != 0 {\n";
  }
  
  // Begin a class declaration.
  void BeginClass(const StructDef &struct_def, std::string *code_ptr) {
    std::string &code = *code_ptr;
  
    code += "type " + struct_def.name + " struct {\n\t";
  
    // _ is reserved in flatbuffers field names, so no chance of name conflict:
    code += "_tab ";
    code += struct_def.fixed ? "flatbuffers.Struct" : "flatbuffers.Table";
    code += "\n}\n\n";
  }
  
  // Begin enum code with a class declaration.
  void BeginEnum(std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "const (\n";
  }
  
  // A single enum member.
  void EnumMember(const EnumDef &enum_def, const EnumVal ev,
                         std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "\t";
    code += enum_def.name;
    code += ev.name;
    code += " = ";
    code += NumToString(ev.value) + "\n";
  }
  
  // End enum code.
  void EndEnum(std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += ")\n\n";
  }
  
  // Begin enum name code.
  void BeginEnumNames(const EnumDef &enum_def, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "var EnumNames";
    code += enum_def.name;
    code += " = map[int]string{\n";
  }
  
  // A single enum name member.
  void EnumNameMember(const EnumDef &enum_def, const EnumVal ev,
                             std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "\t";
    code += enum_def.name;
    code += ev.name;
    code += ":\"";
    code += ev.name;
    code += "\",\n";
  }
  
  // End enum name code.
  void EndEnumNames(std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "}\n\n";
  }
  
  // Initialize a new struct or table from existing data.
  void NewRootTypeFromBuffer(const StructDef &struct_def,
                                    std::string *code_ptr) {
    std::string &code = *code_ptr;
  
    code += "func GetRootAs";
    code += struct_def.name;
    code += "(buf []byte, offset flatbuffers.UOffsetT) ";
    code += "*" + struct_def.name + "";
    code += " {\n";
    code += "\tn := flatbuffers.GetUOffsetT(buf[offset:])\n";
    code += "\tx := &" + struct_def.name + "{}\n";
    code += "\tx.Init(buf, n+offset)\n";
    code += "\treturn x\n";
    code += "}\n\n";
  }
  
  // Initialize an existing object with other data, to avoid an allocation.
  void InitializeExisting(const StructDef &struct_def,
                                 std::string *code_ptr) {
    std::string &code = *code_ptr;
  
    GenReceiver(struct_def, code_ptr);
    code += " Init(buf []byte, i flatbuffers.UOffsetT) ";
    code += "{\n";
    code += "\trcv._tab.Bytes = buf\n";
    code += "\trcv._tab.Pos = i\n";
    code += "}\n\n";
  }
  
  // Implement the table accessor
  void GenTableAccessor(const StructDef &struct_def,
                                 std::string *code_ptr) {
    std::string &code = *code_ptr;
  
    GenReceiver(struct_def, code_ptr);
    code += " Table() flatbuffers.Table ";
    code += "{\n";
  
    if (struct_def.fixed) {
        code += "\treturn rcv._tab.Table\n";
    } else {
        code += "\treturn rcv._tab\n";
    }
    code += "}\n\n";
  }
  
  // Get the length of a vector.
  void GetVectorLen(const StructDef &struct_def,
                           const FieldDef &field,
                           std::string *code_ptr) {
    std::string &code = *code_ptr;
  
    GenReceiver(struct_def, code_ptr);
    code += " " + MakeCamel(field.name) + "Length(";
    code += ") int " + OffsetPrefix(field);
    code += "\t\treturn rcv._tab.VectorLen(o)\n\t}\n";
    code += "\treturn 0\n}\n\n";
  }
  
  // Get a [ubyte] vector as a byte slice.
  void GetUByteSlice(const StructDef &struct_def,
                            const FieldDef &field,
                            std::string *code_ptr) {
    std::string &code = *code_ptr;
  
    GenReceiver(struct_def, code_ptr);
    code += " " + MakeCamel(field.name) + "Bytes(";
    code += ") []byte " + OffsetPrefix(field);
    code += "\t\treturn rcv._tab.ByteVector(o + rcv._tab.Pos)\n\t}\n";
    code += "\treturn nil\n}\n\n";
  }
  
  // Get the value of a struct's scalar.
  void GetScalarFieldOfStruct(const StructDef &struct_def,
                                     const FieldDef &field,
                                     std::string *code_ptr) {
    std::string &code = *code_ptr;
    std::string getter = GenGetter(field.value.type);
    GenReceiver(struct_def, code_ptr);
    code += " " + MakeCamel(field.name);
    code += "() " + TypeName(field) + " {\n";
    code +="\treturn " + getter;
    code += "(rcv._tab.Pos + flatbuffers.UOffsetT(";
    code += NumToString(field.value.offset) + "))\n}\n";
  }
  
  // Get the value of a table's scalar.
  void GetScalarFieldOfTable(const StructDef &struct_def,
                                    const FieldDef &field,
                                    std::string *code_ptr) {
    std::string &code = *code_ptr;
    std::string getter = GenGetter(field.value.type);
    GenReceiver(struct_def, code_ptr);
    code += " " + MakeCamel(field.name);
    code += "() " + TypeName(field) + " ";
    code += OffsetPrefix(field) + "\t\treturn " + getter;
    code += "(o + rcv._tab.Pos)\n\t}\n";
    code += "\treturn " + field.value.constant + "\n";
    code += "}\n\n";
  }
  
  // Get a struct by initializing an existing struct.
  // Specific to Struct.
  void GetStructFieldOfStruct(const StructDef &struct_def,
                                     const FieldDef &field,
                                     std::string *code_ptr) {
    std::string &code = *code_ptr;
    GenReceiver(struct_def, code_ptr);
    code += " " + MakeCamel(field.name);
    code += "(obj *" + TypeName(field);
    code += ") *" + TypeName(field);
    code += " {\n";
    code += "\tif obj == nil {\n";
    code += "\t\tobj = new(" + TypeName(field) + ")\n";
    code += "\t}\n";
    code += "\tobj.Init(rcv._tab.Bytes, rcv._tab.Pos+";
    code += NumToString(field.value.offset) + ")";
    code += "\n\treturn obj\n";
    code += "}\n";
  }
  
  // Get a struct by initializing an existing struct.
  // Specific to Table.
  void GetStructFieldOfTable(const StructDef &struct_def,
                                    const FieldDef &field,
                                    std::string *code_ptr) {
    std::string &code = *code_ptr;
    GenReceiver(struct_def, code_ptr);
    code += " " + MakeCamel(field.name);
    code += "(obj *";
    code += TypeName(field);
    code += ") *" + TypeName(field) + " " + OffsetPrefix(field);
    if (field.value.type.struct_def->fixed) {
      code += "\t\tx := o + rcv._tab.Pos\n";
    } else {
      code += "\t\tx := rcv._tab.Indirect(o + rcv._tab.Pos)\n";
    }
    code += "\t\tif obj == nil {\n";
    code += "\t\t\tobj = new(" + TypeName(field) + ")\n";
    code += "\t\t}\n";
    code += "\t\tobj.Init(rcv._tab.Bytes, x)\n";
    code += "\t\treturn obj\n\t}\n\treturn nil\n";
    code += "}\n\n";
  }
  
  // Get the value of a string.
  void GetStringField(const StructDef &struct_def,
                             const FieldDef &field,
                             std::string *code_ptr) {
    std::string &code = *code_ptr;
    GenReceiver(struct_def, code_ptr);
    code += " " +  MakeCamel(field.name);
    code += "() " + TypeName(field) + " ";
    code += OffsetPrefix(field) + "\t\treturn " + GenGetter(field.value.type);
    code += "(o + rcv._tab.Pos)\n\t}\n\treturn nil\n";
    code += "}\n\n";
  }
  
  // Get the value of a union from an object.
  void GetUnionField(const StructDef &struct_def,
                            const FieldDef &field,
                            std::string *code_ptr) {
    std::string &code = *code_ptr;
    GenReceiver(struct_def, code_ptr);
    code += " " + MakeCamel(field.name) + "(";
    code += "obj " + TypeName(field) + ") bool ";
    code += OffsetPrefix(field);
    code += "\t\t" + GenGetter(field.value.type);
    code += "(obj, o)\n\t\treturn true\n\t}\n";
    code += "\treturn false\n";
    code += "}\n\n";
  }
  
  // Get the value of a vector's struct member.
  void GetMemberOfVectorOfStruct(const StructDef &struct_def,
                                        const FieldDef &field,
                                        std::string *code_ptr) {
    std::string &code = *code_ptr;
    auto vectortype = field.value.type.VectorType();
  
    GenReceiver(struct_def, code_ptr);
    code += " " + MakeCamel(field.name);
    code += "(obj *" + TypeName(field);
    code += ", j int) bool " + OffsetPrefix(field);
    code += "\t\tx := rcv._tab.Vector(o)\n";
    code += "\t\tx += flatbuffers.UOffsetT(j) * ";
    code += NumToString(InlineSize(vectortype)) + "\n";
    if (!(vectortype.struct_def->fixed)) {
      code += "\t\tx = rcv._tab.Indirect(x)\n";
    }
    code += "\t\tobj.Init(rcv._tab.Bytes, x)\n";
    code += "\t\treturn true\n\t}\n";
    code += "\treturn false\n";
    code += "}\n\n";
  }
  
  // Get the value of a vector's non-struct member. Uses a named return
  // argument to conveniently set the zero value for the result.
  void GetMemberOfVectorOfNonStruct(const StructDef &struct_def,
                                           const FieldDef &field,
                                           std::string *code_ptr) {
    std::string &code = *code_ptr;
    auto vectortype = field.value.type.VectorType();
  
    GenReceiver(struct_def, code_ptr);
    code += " " + MakeCamel(field.name);
    code += "(j int) " + TypeName(field) + " ";
    code += OffsetPrefix(field);
    code += "\t\ta := rcv._tab.Vector(o)\n";
    code += "\t\treturn " + GenGetter(field.value.type) + "(";
    code += "a + flatbuffers.UOffsetT(j*";
    code += NumToString(InlineSize(vectortype)) + "))\n";
    code += "\t}\n";
    if (vectortype.base_type == BASE_TYPE_STRING) {
      code += "\treturn nil\n";
    } else {
      code += "\treturn 0\n";
    }
    code += "}\n\n";
  }
  
  // Begin the creator function signature.
  void BeginBuilderArgs(const StructDef &struct_def,
                               std::string *code_ptr) {
    std::string &code = *code_ptr;
  
    if (code.substr(code.length() - 2) != "\n\n") {
        // a previous mutate has not put an extra new line
        code += "\n";
    }
    code += "func Create" + struct_def.name;
    code += "(builder *flatbuffers.Builder";
  }
  
  // Recursively generate arguments for a constructor, to deal with nested
  // structs.
  void StructBuilderArgs(const StructDef &struct_def,
                                const char *nameprefix,
                                std::string *code_ptr) {
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end();
         ++it) {
      auto &field = **it;
      if (IsStruct(field.value.type)) {
        // Generate arguments for a struct inside a struct. To ensure names
        // don't clash, and to make it obvious these arguments are constructing
        // a nested struct, prefix the name with the field name.
        StructBuilderArgs(*field.value.type.struct_def,
                          (nameprefix + (field.name + "_")).c_str(),
                          code_ptr);
      } else {
        std::string &code = *code_ptr;
        code += (std::string)", " + nameprefix;
        code += MakeCamel(field.name, false);
        code += " " + GenTypeBasic(field.value.type);
      }
    }
  }
  
  // End the creator function signature.
  void EndBuilderArgs(std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += ") flatbuffers.UOffsetT {\n";
  }
  
  // Recursively generate struct construction statements and instert manual
  // padding.
  void StructBuilderBody(const StructDef &struct_def,
                                const char *nameprefix,
                                std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "\tbuilder.Prep(" + NumToString(struct_def.minalign) + ", ";
    code += NumToString(struct_def.bytesize) + ")\n";
    for (auto it = struct_def.fields.vec.rbegin();
         it != struct_def.fields.vec.rend();
         ++it) {
      auto &field = **it;
      if (field.padding)
        code += "\tbuilder.Pad(" + NumToString(field.padding) + ")\n";
      if (IsStruct(field.value.type)) {
        StructBuilderBody(*field.value.type.struct_def,
                          (nameprefix + (field.name + "_")).c_str(),
                          code_ptr);
      } else {
        code += "\tbuilder.Prepend" + GenMethod(field) + "(";
        code += nameprefix + MakeCamel(field.name, false) + ")\n";
      }
    }
  }
  
  void EndBuilderBody(std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "\treturn builder.Offset()\n";
    code += "}\n";
  }
  
  // Get the value of a table's starting offset.
  void GetStartOfTable(const StructDef &struct_def,
                              std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "func " + struct_def.name + "Start";
    code += "(builder *flatbuffers.Builder) {\n";
    code += "\tbuilder.StartObject(";
    code += NumToString(struct_def.fields.vec.size());
    code += ")\n}\n";
  }
  
  // Set the value of a table's field.
  void BuildFieldOfTable(const StructDef &struct_def,
                                const FieldDef &field,
                                const size_t offset,
                                std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "func " + struct_def.name + "Add" + MakeCamel(field.name);
    code += "(builder *flatbuffers.Builder, ";
    code += MakeCamel(field.name, false) + " ";
    if (!IsScalar(field.value.type.base_type) && (!struct_def.fixed)) {
      code += "flatbuffers.UOffsetT";
    } else {
      code += GenTypeBasic(field.value.type);
    }
    code += ") {\n";
    code += "\tbuilder.Prepend";
    code += GenMethod(field) + "Slot(";
    code += NumToString(offset) + ", ";
    if (!IsScalar(field.value.type.base_type) && (!struct_def.fixed)) {
      code += "flatbuffers.UOffsetT";
      code += "(";
      code += MakeCamel(field.name, false) + ")";
    } else {
      code += MakeCamel(field.name, false);
    }
    code += ", " + field.value.constant;
    code += ")\n}\n";
  }
  
  // Set the value of one of the members of a table's vector.
  void BuildVectorOfTable(const StructDef &struct_def,
                                 const FieldDef &field,
                                 std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "func " + struct_def.name + "Start";
    code += MakeCamel(field.name);
    code += "Vector(builder *flatbuffers.Builder, numElems int) ";
    code += "flatbuffers.UOffsetT {\n\treturn builder.StartVector(";
    auto vector_type = field.value.type.VectorType();
    auto alignment = InlineAlignment(vector_type);
    auto elem_size = InlineSize(vector_type);
    code += NumToString(elem_size);
    code += ", numElems, " + NumToString(alignment);
    code += ")\n}\n";
  }
  
  // Get the offset of the end of a table.
  void GetEndOffsetOnTable(const StructDef &struct_def,
                                  std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "func " + struct_def.name + "End";
    code += "(builder *flatbuffers.Builder) flatbuffers.UOffsetT ";
    code += "{\n\treturn builder.EndObject()\n}\n";
  }
  
  // Generate the receiver for function signatures.
  void GenReceiver(const StructDef &struct_def, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "func (rcv *" + struct_def.name + ")";
  }
  
  // Generate a struct field getter, conditioned on its child type(s).
  void GenStructAccessor(const StructDef &struct_def,
                                const FieldDef &field,
                                std::string *code_ptr) {
    GenComment(field.doc_comment, code_ptr, nullptr, "");
    if (IsScalar(field.value.type.base_type)) {
      if (struct_def.fixed) {
        GetScalarFieldOfStruct(struct_def, field, code_ptr);
      } else {
        GetScalarFieldOfTable(struct_def, field, code_ptr);
      }
    } else {
      switch (field.value.type.base_type) {
        case BASE_TYPE_STRUCT:
          if (struct_def.fixed) {
            GetStructFieldOfStruct(struct_def, field, code_ptr);
          } else {
            GetStructFieldOfTable(struct_def, field, code_ptr);
          }
          break;
        case BASE_TYPE_STRING:
          GetStringField(struct_def, field, code_ptr);
          break;
        case BASE_TYPE_VECTOR: {
          auto vectortype = field.value.type.VectorType();
          if (vectortype.base_type == BASE_TYPE_STRUCT) {
            GetMemberOfVectorOfStruct(struct_def, field, code_ptr);
          } else {
            GetMemberOfVectorOfNonStruct(struct_def, field, code_ptr);
          }
          break;
        }
        case BASE_TYPE_UNION:
          GetUnionField(struct_def, field, code_ptr);
          break;
        default:
          assert(0);
      }
    }
    if (field.value.type.base_type == BASE_TYPE_VECTOR) {
      GetVectorLen(struct_def, field, code_ptr);
      if (field.value.type.element == BASE_TYPE_UCHAR) {
        GetUByteSlice(struct_def, field, code_ptr);
      }
    }
  }
  
  // Mutate the value of a struct's scalar.
  void MutateScalarFieldOfStruct(const StructDef &struct_def,
                                     const FieldDef &field,
                                     std::string *code_ptr) {
    std::string &code = *code_ptr;
    std::string type = MakeCamel(GenTypeBasic(field.value.type));
    std::string setter = "rcv._tab.Mutate" + type;
    GenReceiver(struct_def, code_ptr);
    code += " Mutate" + MakeCamel(field.name);
    code += "(n " + TypeName(field) + ") bool {\n\treturn " + setter;
    code += "(rcv._tab.Pos+flatbuffers.UOffsetT(";
    code += NumToString(field.value.offset) + "), n)\n}\n\n";
  }
  
  // Mutate the value of a table's scalar.
  void MutateScalarFieldOfTable(const StructDef &struct_def,
                                    const FieldDef &field,
                                    std::string *code_ptr) {
    std::string &code = *code_ptr;
    std::string type = MakeCamel(GenTypeBasic(field.value.type));
    std::string setter = "rcv._tab.Mutate" + type + "Slot";
    GenReceiver(struct_def, code_ptr);
    code += " Mutate" + MakeCamel(field.name);
    code += "(n " + TypeName(field) + ") bool {\n\treturn ";
    code += setter + "(" + NumToString(field.value.offset) + ", n)\n";
    code += "}\n\n";
  }
  
  // Generate a struct field setter, conditioned on its child type(s).
  void GenStructMutator(const StructDef &struct_def,
                                const FieldDef &field,
                                std::string *code_ptr) {
    GenComment(field.doc_comment, code_ptr, nullptr, "");
    if (IsScalar(field.value.type.base_type)) {
      if (struct_def.fixed) {
        MutateScalarFieldOfStruct(struct_def, field, code_ptr);
      } else {
        MutateScalarFieldOfTable(struct_def, field, code_ptr);
      }
    }
  }
  
  // Generate table constructors, conditioned on its members' types.
  void GenTableBuilders(const StructDef &struct_def,
                               std::string *code_ptr) {
    GetStartOfTable(struct_def, code_ptr);
  
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end();
         ++it) {
      auto &field = **it;
      if (field.deprecated) continue;
  
      auto offset = it - struct_def.fields.vec.begin();
      BuildFieldOfTable(struct_def, field, offset, code_ptr);
      if (field.value.type.base_type == BASE_TYPE_VECTOR) {
        BuildVectorOfTable(struct_def, field, code_ptr);
      }
    }
  
    GetEndOffsetOnTable(struct_def, code_ptr);
  }
  
  // Generate struct or table methods.
  void GenStruct(const StructDef &struct_def,
                        std::string *code_ptr) {
    if (struct_def.generated) return;
  
    GenComment(struct_def.doc_comment, code_ptr, nullptr);
    BeginClass(struct_def, code_ptr);
    if (!struct_def.fixed) {
      // Generate a special accessor for the table that has been declared as
      // the root type.
      NewRootTypeFromBuffer(struct_def, code_ptr);
    }
    // Generate the Init method that sets the field in a pre-existing
    // accessor object. This is to allow object reuse.
    InitializeExisting(struct_def, code_ptr);
    // Generate _tab accessor
    GenTableAccessor(struct_def, code_ptr);
  
    // Generate struct fields accessors
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end();
         ++it) {
      auto &field = **it;
      if (field.deprecated) continue;
  
      GenStructAccessor(struct_def, field, code_ptr);
      GenStructMutator(struct_def, field, code_ptr);
    }
  
    // Generate builders
    if (struct_def.fixed) {
      // create a struct constructor function
      GenStructBuilder(struct_def, code_ptr);
    } else {
      // Create a set of functions that allow table construction.
      GenTableBuilders(struct_def, code_ptr);
    }
  }
  
  // Generate enum declarations.
  void GenEnum(const EnumDef &enum_def, std::string *code_ptr) {
    if (enum_def.generated) return;
  
    GenComment(enum_def.doc_comment, code_ptr, nullptr);
    BeginEnum(code_ptr);
    for (auto it = enum_def.vals.vec.begin();
         it != enum_def.vals.vec.end();
         ++it) {
      auto &ev = **it;
      GenComment(ev.doc_comment, code_ptr, nullptr, "\t");
      EnumMember(enum_def, ev, code_ptr);
    }
    EndEnum(code_ptr);
  
    BeginEnumNames(enum_def, code_ptr);
    for (auto it = enum_def.vals.vec.begin();
         it != enum_def.vals.vec.end();
         ++it) {
      auto &ev = **it;
      EnumNameMember(enum_def, ev, code_ptr);
    }
    EndEnumNames(code_ptr);
  }
  
  // Returns the function name that is able to read a value of the given type.
  std::string GenGetter(const Type &type) {
    switch (type.base_type) {
      case BASE_TYPE_STRING: return "rcv._tab.ByteVector";
      case BASE_TYPE_UNION: return "rcv._tab.Union";
      case BASE_TYPE_VECTOR: return GenGetter(type.VectorType());
      default:
        return "rcv._tab.Get" + MakeCamel(GenTypeGet(type));
    }
  }
  
  // Returns the method name for use with add/put calls.
  std::string GenMethod(const FieldDef &field) {
    return IsScalar(field.value.type.base_type)
      ? MakeCamel(GenTypeBasic(field.value.type))
      : (IsStruct(field.value.type) ? "Struct" : "UOffsetT");
  }
  
  std::string GenTypeBasic(const Type &type) {
    static const char *ctypename[] = {
      #define FLATBUFFERS_TD(ENUM, IDLTYPE, CTYPE, JTYPE, GTYPE, NTYPE, PTYPE) \
        #GTYPE,
        FLATBUFFERS_GEN_TYPES(FLATBUFFERS_TD)
      #undef FLATBUFFERS_TD
    };
    return ctypename[type.base_type];
  }
  
  std::string GenTypePointer(const Type &type) {
    switch (type.base_type) {
      case BASE_TYPE_STRING:
        return "[]byte";
      case BASE_TYPE_VECTOR:
        return GenTypeGet(type.VectorType());
      case BASE_TYPE_STRUCT:
        return GenTypeName(*type.struct_def);
      case BASE_TYPE_UNION:
        // fall through
      default:
        return "*flatbuffers.Table";
    }
  }
  
  std::string GenTypeGet(const Type &type) {
    return IsScalar(type.base_type)
      ? GenTypeBasic(type)
      : GenTypePointer(type);
  }
  
  std::string TypeName(const FieldDef &field) {
    return GenTypeGet(field.value.type);
  }
  
  // Create a struct with a builder and the struct's arguments.
  void GenStructBuilder(const StructDef &struct_def,
                               std::string *code_ptr) {
    BeginBuilderArgs(struct_def, code_ptr);
    StructBuilderArgs(struct_def, "", code_ptr);
    EndBuilderArgs(code_ptr);
  
    StructBuilderBody(struct_def, "", code_ptr);
    EndBuilderBody(code_ptr);
  }

};
}  // namespace go

bool GenerateGo(const Parser &parser, const std::string &path,
                const std::string &file_name) {
  go::GoGenerator generator(parser, path, file_name);
  return generator.generate();
}

}  // namespace flatbuffers
