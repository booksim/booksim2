/*
 * workload message 
 *
 * C. Beckmann (c) Intel, 2023
 * H. Dogan
 */

#pragma once

#include <iostream>
#include <vector>
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include "config_utils.hpp"

// new traffic generators will use this traffic class
extern int gInjectorTrafficClass;

// smart pointer to workload messages
class WorkloadMessage;
typedef boost::intrusive_ptr<WorkloadMessage> WorkloadMessagePtr;


//
// workload message.
// Base class for request and reply messages used by workload components
//
class WorkloadMessage : public boost::intrusive_ref_counter<WorkloadMessage>
{
   public:
   enum msg_t {
      AnyRequest=0,
      GetRequest,
      PutRequest,
      NbGetRequest,
      SendRequest,
      RecvRequest,
      DummyRequest
   };

   virtual ~WorkloadMessage() {}

   virtual int Source() const = 0;
   virtual int Dest() const = 0;
   virtual int Size() const = 0;
   virtual int PayloadSize() const = 0;
   virtual msg_t Type() const = 0;
   virtual bool IsReply() const = 0;
   virtual bool IsRead() const = 0;
   virtual bool IsWrite() const = 0;
   virtual bool IsDummy() const = 0;
   virtual bool IsBlocking() const = 0;
   virtual WorkloadMessagePtr Reply() = 0;
   virtual WorkloadMessagePtr Contents() const = 0; // encapsulated message, if any
   virtual std::ostream & Print(std::ostream &, bool deep = false) const;

   // get an encapsulated message of the given type (if none, return NULL)
   template <class T> T * ContentsOfType() {
      for (auto p = this; p; p = p->Contents().get())
         if (T* q = dynamic_cast<T*>(p))
            return q;
      return 0;
   }
};

// return a pointer to encapsulated contents of the given type, or NULL.
template <class T> T * WorkloadMessageContents(WorkloadMessagePtr p)
{
    return p.get() ? p->ContentsOfType<T>() : 0;
}

// primary messages produced by traffic generators
class GeneratorWorkloadMessage : public WorkloadMessage
{
  public:
   // factory helper class initializes overhead values from Booksim knobs, and remembers traffic class for new messages
   class Factory {
     public:
      const int traffic_class;
      Factory(Configuration const *config, int traffic_class);
   };

  protected:
   Factory *const factory; // needed to generate replies

  private:
   const int src;
   const int dest;
   const msg_t type;
   const bool is_reply;
   const int data_payload_size;
   const int size;

   // adders to the message size, from Booksim knobs, indexed by traffic class
   static vector<int> any_overhead;
   static vector<int> write_reply_overhead;
   static vector<int> read_reply_overhead;
   static vector<int> write_request_overhead;
   static vector<int> read_request_overhead;

   int _get_size(bool is_dummy, bool is_any, bool is_reply, bool is_read, int tc) {
      return
         is_dummy
         ?  0
         :  is_any
            ?  data_payload_size + any_overhead[tc]
            :  is_reply
               ?   (is_read
                   ?  data_payload_size + read_reply_overhead[tc]
                   :  write_reply_overhead[tc])
               :   (is_read
                   ?  read_request_overhead[tc]
                   :  data_payload_size + write_request_overhead[tc]);
   }
  public:
   GeneratorWorkloadMessage() = delete;
   // data size is added to overheads, depending on message type
   GeneratorWorkloadMessage(Factory *f, int src_pe, int dest_pe, msg_t msg_type = AnyRequest, bool reply_msg = false, int data_size = 0) :
      factory(f),
      src(src_pe),
      dest(dest_pe),
      type(msg_type),
      is_reply(reply_msg),
      data_payload_size(data_size),
      size(_get_size(IsDummy(), msg_type == AnyRequest, reply_msg, IsRead(), f->traffic_class))
   {}
   // use this constructor to explicitly set size and data_payload_size regardless of config knobs
   GeneratorWorkloadMessage(Factory *f, int src_pe, int dest_pe, msg_t msg_type, bool reply_msg, int size, int payload_size) :
      factory(f),
      src(src_pe),
      dest(dest_pe),
      type(msg_type),
      is_reply(reply_msg),
      data_payload_size(payload_size),
      size(size)
   {}
   // copy- and reply- constructor
   GeneratorWorkloadMessage(const GeneratorWorkloadMessage &m, bool make_reply = false) :
      factory(m.factory),
      src(make_reply ? m.dest : m.src),
      dest(make_reply ? m.src : m.dest),
      type(m.type),
      is_reply(make_reply || m.is_reply),
      data_payload_size(m.data_payload_size),
      // if we're making a reply, use same logic as first constructor form.  For copy constructor, just copy the size:
      size(make_reply ? _get_size(IsDummy(), m.type == AnyRequest, true, IsRead(), m.factory->traffic_class) : m.size)
   {}
   

   virtual int Source() const { return src; }
   virtual int Dest() const { return dest; }
   virtual int Size() const { return size; }
   virtual int PayloadSize() const { return data_payload_size; }
   virtual msg_t Type() const { return type; }
   virtual bool IsReply() const { return is_reply; }
   virtual bool IsRead() const { return type == GetRequest || type == NbGetRequest;  }
   virtual bool IsWrite() const { return type == PutRequest || type == SendRequest;  }
   virtual bool IsDummy() const { return type == DummyRequest;  }
   virtual bool IsBlocking() const { return type == GetRequest || type == RecvRequest;  }
   virtual WorkloadMessagePtr Contents() const { return 0; }

   virtual WorkloadMessagePtr Reply() {
      return new GeneratorWorkloadMessage(*this, true);
   }

};

// modifier components that need to wrap upstream messages should inherit from this
template <class WKLDMSG>
class ModifierWorkloadMessage : public WorkloadMessage
{
  protected:
   WorkloadMessagePtr _contents;

  public:
   ModifierWorkloadMessage(WorkloadMessagePtr msg) : _contents(msg) {}

   // proxy all the API calls, derived classes may override some or all of these
   WorkloadMessagePtr Contents() const { return _contents;       }
   int   Source()      const { return Contents()->Source();      }
   int   Dest()        const { return Contents()->Dest();        }
   int   Size()        const { return Contents()->Size();        }
   int   PayloadSize() const { return Contents()->PayloadSize(); }
   msg_t Type()        const { return Contents()->Type();        }
   bool  IsReply()     const { return Contents()->IsReply();     }
   bool  IsRead()      const { return Contents()->IsRead();      }
   bool  IsWrite()     const { return Contents()->IsWrite();     }
   bool  IsDummy()     const { return Contents()->IsDummy();     }
   bool  IsBlocking()  const { return Contents()->IsBlocking();  }

   WorkloadMessagePtr Reply() {
      // generate and wrap the upstream reply
      return new WKLDMSG(Contents()->Reply());
   }
};

inline std::ostream & operator << (std::ostream & os, const WorkloadMessage & msg)
{
    return msg.Print(os);
}
