// Copyright 2022 ByteDance Ltd. and/or its affiliates.
/*
 * Acknowledgement: This file originates from TVM.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include <matxscript/ir/printer/ir_docsifier.h>

#include <matxscript/runtime/logging.h>
#include <matxscript/runtime/registry.h>

#include <matxscript/ir/printer/utils.h>

namespace matxscript {
namespace ir {
namespace printer {

IdDoc IRDocsifierNode::Define(const ObjectRef& obj,
                              const Frame& frame,
                              const StringRef& name_hint) {
  MXCHECK(obj2info.find(obj) == obj2info.end()) << "Duplicated object: " << obj;
  StringRef name = GenerateUniqueName(name_hint, this->defined_names);
  this->defined_names.insert(name);
  DocCreator doc_factory = [name]() { return IdDoc(name); };
  obj2info.insert({obj, VariableInfo{std::move(doc_factory), name}});
  IdDoc def_doc(name);
  frame->AddExitCallback([this, obj]() { this->RemoveVar(obj); });
  return def_doc;
}

void IRDocsifierNode::Define(const ObjectRef& obj, const Frame& frame, DocCreator doc_factory) {
  MXCHECK(obj2info.find(obj) == obj2info.end()) << "Duplicated object: " << obj;
  obj2info.insert({obj, VariableInfo{std::move(doc_factory), NullOpt}});
  frame->AddExitCallback([this, obj]() { this->RemoveVar(obj); });
}

Optional<ExprDoc> IRDocsifierNode::GetVarDoc(const ObjectRef& obj) const {
  auto it = obj2info.find(obj);
  if (it == obj2info.end()) {
    return NullOpt;
  }
  return it->second.creator();
}

ExprDoc IRDocsifierNode::AddMetadata(const ObjectRef& obj) {
  MXCHECK(obj.defined()) << "TypeError: Cannot add nullptr to metadata";
  StringRef key(obj->GetTypeKey());
  Array<ObjectRef>& array = metadata[key];
  int index = std::find(array.begin(), array.end(), obj) - array.begin();
  if (index == static_cast<int>(array.size())) {
    array.push_back(obj);
  }
  return IdDoc("metadata")[{LiteralDoc::Str(key, NullOpt)}][{LiteralDoc::Int(index, NullOpt)}];
}

bool IRDocsifierNode::IsVarDefined(const ObjectRef& obj) const {
  return obj2info.count(obj);
}

void IRDocsifierNode::RemoveVar(const ObjectRef& obj) {
  auto it = obj2info.find(obj);
  MXCHECK(it != obj2info.end()) << "No such object: " << obj;
  if (it->second.name.defined()) {
    defined_names.erase(it->second.name.value());
  }
  obj2info.erase(it);
}

void IRDocsifierNode::SetCommonPrefix(const ObjectRef& root,
                                      runtime::TypedNativeFunction<bool(ObjectRef)> is_var) {
  class Visitor : public AttrVisitor {
   public:
    inline void operator()(ObjectRef obj) {
      Visit("", &obj);
    }

   private:
    void Visit(const char* key, double* value) final {
    }
    void Visit(const char* key, int64_t* value) final {
    }
    void Visit(const char* key, uint64_t* value) final {
    }
    void Visit(const char* key, int* value) final {
    }
    void Visit(const char* key, bool* value) final {
    }
    void Visit(const char* key, runtime::String* value) final {
    }
    void Visit(const char* key, void** value) final {
    }
    void Visit(const char* key, DataType* value) final {
    }
    void Visit(const char* key, runtime::NDArray* value) final {
    }
    void Visit(const char* key, ObjectRef* value) final {
      const Object* obj = value->get();
      if (visited.count(obj)) {
        return;
      }
      visited.emplace(obj);
      if (obj == nullptr) {
        return;
      }
      stack_.push_back(obj);
      if (obj->IsInstance<ArrayNode>()) {
        const ArrayNode* array = static_cast<const ArrayNode*>(obj);
        for (ObjectRef element : *array) {
          this->Visit("", &element);
        }
      } else if (obj->IsInstance<MapNode>()) {
        const MapNode* map = static_cast<const MapNode*>(obj);
        for (std::pair<ObjectRef, ObjectRef> kv : *map) {
          this->Visit("", &kv.first);
          this->Visit("", &kv.second);
        }
      } else {
        vtable_->VisitAttrs(const_cast<Object*>(obj), this);
      }
      if (is_var(runtime::GetRef<ObjectRef>(obj))) {
        HandleVar(obj);
      }
      stack_.pop_back();
    }

    void HandleVar(const Object* var) {
      if (common_prefix.count(var) == 0) {
        common_prefix[var] = stack_;
        return;
      }
      std::vector<const Object*>& a = common_prefix[var];
      std::vector<const Object*>& b = stack_;
      int n = std::min(a.size(), b.size());
      for (int i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
          a.resize(i);
          break;
        }
      }
    }

    ReflectionVTable* vtable_ = ReflectionVTable::Global();
    std::vector<const Object*> stack_;

   public:
    runtime::TypedNativeFunction<bool(ObjectRef)> is_var;
    std::unordered_map<const Object*, std::vector<const Object*>> common_prefix;
    std::unordered_set<const Object*> visited;
  };
  Visitor visitor;
  visitor.is_var = is_var;
  visitor(root);
  this->common_prefix = std::move(visitor.common_prefix);
}

IRDocsifier::IRDocsifier(const PrinterConfig& cfg) {
  auto n = runtime::make_object<IRDocsifierNode>();
  n->cfg = cfg;
  n->dispatch_tokens.push_back("");
  data_ = std::move(n);
}

IRDocsifier::FType& IRDocsifier::vtable() {
  static IRDocsifier::FType inst;
  return inst;
}

MATXSCRIPT_REGISTER_NODE_TYPE(FrameNode);
MATXSCRIPT_REGISTER_NODE_TYPE(IRDocsifierNode);

}  // namespace printer
}  // namespace ir
}  // namespace matxscript
