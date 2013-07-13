// -*- compile-command: "cd ../tests && make" -*-

// multimethod.cpp
// Copyright (c) 2013 Jean-Louis Leroy
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <multimethods.hpp>

using namespace std;

namespace multimethods {

  mm_class::mm_class(const std::type_info& t) : ti(t), abstract(false) {
    //std::cout << "mm_class() for " << ti.name() << " at " << this << endl;
  }

  mm_class::~mm_class() {
    //cout << "~mm_class() at " << this << endl;
  }
  
  void mm_class::for_each_conforming(std::function<void(mm_class*)> pf) {
    pf(this);
    for_each(specs.begin(), specs.end(),
             [=](mm_class* p) { p->for_each_conforming(pf); });
  }

  bool mm_class::conforms_to(const mm_class& other) const {
    // true if same class or this is derived from other
    using namespace std;
    //cout << name() << " <? " << other.name() << endl;
    
    return this == &other ||
      std::any_of(bases.begin(), bases.end(), [&](mm_class* base) {
          return base->conforms_to(other);
        });
  }

  bool mm_class::specializes(const mm_class& other) const {
    return std::any_of(bases.begin(), bases.end(), [&](mm_class* base) {
        return base == &other || base->specializes(other);
      });
  }

  method_base method_base::undefined;
  method_base method_base::ambiguous;

  bool method_base::specializes(const method_base* other) const {

    if (this == other) {
      return false;
    }
    
    bool result = false;
    
    for (int dim = 0; dim < args.size(); dim++) {
      if (args[dim] != other->args[dim]) {
        if (args[dim]->specializes(*other->args[dim])) {
          result = true;
        } else {
          return false;
        }
      }
    }

    return result;
  }

  void mm_class::insert_slot(int i) {
    MM_TRACE(std::cout << "allocate slot in " << ti.name() << " at " << i << "\n");
    mmt.insert(mmt.begin() + i, 1, 0);
    std::for_each(mms.begin(), mms.end(), [=](multimethod_base* pm) { pm->shift(i); });
  }

  int mm_class::add_multimethod(multimethod_base* pm) {
    int slot = mmt.size();
    for_each_conforming(std::mem_fun(&mm_class::reserve_slot));
    for_each_conforming([=](mm_class* pc) { pc->insert_slot(slot); });
    mms.push_back(pm);
    return slot;
  }

  std::unordered_map<std::type_index, const std::vector<int>*> get_mm_table<false>::types;
  
  std::ostream& operator <<(std::ostream& os, std::vector<mm_class*> classes) {
    using namespace std;
    const char* sep = "(";

    for (mm_class* pc : classes) {
      os << sep;
      sep = ", ";
      os << pc;
    }

    return os << ")";
  }
  
  multimethod_base::multimethod_base(const std::vector<mm_class*>& v) : vargs(v), ready(false) {
      int i = 0;
      for_each(vargs.begin(), vargs.end(),
               [&](mm_class* pc) {
                 MM_TRACE(std::cout << "add mm rooted in " << pc->ti.name() << " argument " << i << "\n");
                 int slot = pc->add_multimethod(this);
                 MM_TRACE(std::cout << "-> at slot " << slot << "\n");
                 ++i;
                 slots.push_back(slot);
               });
    }
  
  void multimethod_base::shift(int pos) {
    for (int& slot : slots) {
      if (slot >= pos) {
        ++slot;
      }
    }

    invalidate();
  }
  
  void resolver::make_steps() {
    mm.steps.resize(dims);
    int step = 1;

    for (int i = 0, n = dims; i < n; i++) {
      mm.steps[i] = step;
      step *= classes[i].size();
    }

    dispatch_table_size = step;
  }

  int resolver::linear(const std::vector<int>& tuple) {
    auto tuple_iter = tuple.begin();
    auto step_iter = mm.steps.begin();
    int offset = 0;
    
    while (tuple_iter != tuple.end()) {
      offset += *tuple_iter++ * *step_iter++;
    }

    return offset;
  }

  void resolver::assign_class_indices() {
    classes.resize(dims);
  
    int dim = 0;
  
    for (mm_class* pc : mm.vargs) {
      int i = 0;
      std::vector<mm_class*>& dim_classes = classes[dim];
      pc->for_each_conforming([&](mm_class* pc) {
          pc->mmt[mm.slots[dim]] = i++;
          dim_classes.push_back(pc);
        });
    
      ++dim;
    }
  }

  resolver::resolver(multimethod_base& mm) : mm(mm), dims(mm.vargs.size()) {
    assign_class_indices();
    make_steps();
  }

  void resolver::resolve(multimethod_base::emit_func emit, multimethod_base::emit_next_func emit_next) {
    this->emit = emit;
    emit_at = 0;
    this->emit_next = emit_next;
    tuple.resize(dims);
    std::fill(tuple.begin(), tuple.end(), 0);
    do_resolve(tuple.size() - 1, mm.methods);
    assign_next();
  }

  void resolver::assign_next() {
    for (method_base* pm : mm.methods) {
      vector<method_base*> candidates;
      copy_if(
        mm.methods.begin(), mm.methods.end(), back_inserter(candidates),
        [&](method_base* other) {
          return pm != other && pm->specializes(other);
        });
      MM_TRACE(cout << "calculating next for " << pm << ", candidates:\n");
      MM_TRACE(copy(candidates.begin(), candidates.end(), ostream_iterator<const method_base*>(cout, "\n")));
      auto best = find_best(candidates);
      MM_TRACE(cout << "next is: " << best << endl);
      emit_next(pm, best);
    }
  }

  void resolver::do_resolve(int dim, const std::vector<method_base*>& candidates) {
    using namespace std;
    MM_TRACE(cout << "\nresolve dim = " << dim << endl);
           
    for (int cls = 0; cls < classes[dim].size(); cls++) {
      tuple[dim] = cls;
      mm_class* pc = classes[dim][cls];
      MM_TRACE(cout << "--- tuple = (");
      MM_TRACE({ int i = 0; transform(
            tuple.begin(), tuple.end() - 1, ostream_iterator<mm_class*>(cout, ", "),
            [&](int c) { return classes[i][c]; }); });
      MM_TRACE(cout << classes[tuple.size() - 1][tuple.back()] << ")\n");

      vector<method_base*> viable;
             
      copy_if(
        candidates.begin(), candidates.end(), back_inserter(viable),
        [&](method_base* method) {
          return pc->conforms_to(*method->args[dim]);
        });

      MM_TRACE(cout << "viable = " << flush);
      MM_TRACE(copy(viable.begin(), viable.end(), ostream_iterator<const method_base*>(cout, " ")));
      MM_TRACE(cout << endl);

      if (dim == 0) {
        auto best = find_best(viable);
        MM_TRACE(cout << "install " << best << " at offset " << emit_at << endl); 
        emit(best, emit_at++);
      } else {
        do_resolve(dim - 1, viable);
      }
    }
    
    MM_TRACE(cout << "exiting dim " << dim << endl);
  }

  int resolver::order(const method_base* a, const method_base* b) {

    using namespace std;

    return a == b ? 0 : a->specializes(b) ? -1 : b->specializes(a) ? 1 : 0;
  
    // if (a == b) {
    //   return 0;
    // }

    // auto b_arg_iter = b->args.begin();
    // bool a_specializes_b = any_of(a->args.begin(), a->args.end(), [&](const mm_class* a_arg) {
    //     return a_arg->specializes(**b_arg_iter++);
    //   });

    // auto a_arg_iter = a->args.begin();
    // bool b_specializes_a = any_of(b->args.begin(), b->args.end(), [&](const mm_class* b_arg) {
    //     return b_arg->specializes(**a_arg_iter++);
    //   });

    // if (a_specializes_b) {
    //   return b_specializes_a ? 0 : -1;
    // } else {
    //   return b_specializes_a ? 1 : 0;
    // }
  }
  
  method_base* resolver::find_best(const std::vector<method_base*>& methods) {
    if (methods.empty()) {
      return &method_base::undefined;
    }

    using namespace std;

    vector<method_base*> best;

    for (method_base* method : methods) {
      auto best_iter = best.begin();

      while (best_iter != best.end()) {
        switch (order(method, *best_iter)) {
        case -1:
          MM_TRACE(cout << method << " specializes " << *best_iter << ", removed\n");
          best.erase(best_iter);
          break;
        case 0:
          best_iter++;
          break;
        case 1:
          MM_TRACE(cout << *best_iter << " specializes " << method << ", removed\n");
          best_iter = best.end();
          method = 0;
          break;
        }
      }

      if (method) {
        MM_TRACE(cout << method << " kept\n");
        best.push_back(method);
      }
    }
      
    return best.size() == 0 ? &method_base::undefined
      : best.size() == 1 ? best.front()
      : &method_base::ambiguous;
  }

  std::ostream& operator <<(std::ostream& os, const method_base* method) {
    using namespace std;

    if (method == &method_base::undefined) {
      return os << "undefined";
    }

    if (method == &method_base::ambiguous) {
      return os << "ambiguous";
    }
    
    const char* sep = "method(";
  
    for (mm_class* pc : method->args) {
      os << sep;
      sep = ", ";
      os << pc->name();
    }

    return os << ")";
  }
  
  std::ostream& operator <<(std::ostream& os, const std::vector<method_base*>& methods) {
    using namespace std;
    const char* sep = "";
  
    for (method_base* pm : methods) {
      os << sep;
      sep = " ";
      os << pm;
    }

    return os;
  }
}
