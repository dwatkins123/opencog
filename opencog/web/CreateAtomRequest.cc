/*
 * opencog/rest/CreateAtomRequest.cc
 *
 * Copyright (C) 2010 by Singularity Institute for Artificial Intelligence
 * All Rights Reserved
 *
 * Written by Joel Pitt <joel@fruitionnz.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "CreateAtomRequest.h"
#include "BaseURLHandler.h"

#include <opencog/atomspace/AtomSpace.h>
#include <opencog/atomspace/ClassServer.h>
#include <opencog/atomspace/TLB.h>
#include <opencog/atomspace/TruthValue.h>
#include <opencog/atomspace/IndefiniteTruthValue.h>
#include <opencog/atomspace/SimpleTruthValue.h>
#include <opencog/atomspace/CountTruthValue.h>
#include <opencog/atomspace/CompositeTruthValue.h>
#include <opencog/atomspace/types.h>
#include <opencog/server/CogServer.h>

#include <boost/algorithm/string.hpp>

#include <opencog/web/json_spirit/json_spirit.h>

using namespace opencog;
using namespace json_spirit;

CreateAtomRequest::CreateAtomRequest()
{
}

CreateAtomRequest::~CreateAtomRequest()
{
    logger().debug("[CreateAtomRequest] destructor");
}

void CreateAtomRequest::setRequestResult(RequestResult* rr)
{
    Request::setRequestResult(rr);
    rr->SetDataRequest();
}

bool CreateAtomRequest::execute()
{
    std::string& data = _parameters.front();
    AtomSpace* as = server().getAtomSpace();

    Type t = NOTYPE;
    std::string atomName;
    HandleSeq outgoing;
    TruthValue* tv = NULL;
    Value json_top;
    try {
        read( data, json_top);
        const Object &json_obj = json_top.get_obj();
        for( Object::size_type i = 0; i != json_obj.size(); ++i ) {
            std::string s;
            const Pair& pair = json_obj[i];
            const std::string& name = pair.name_;
            const Value&  value = pair.value_;
            if (name == "type") {
                t = classserver().getType(value.get_str());
            } else if (name == "name") {
                atomName = value.get_str();
            } else if (name == "outgoing") {
                const Array &outArray = value.get_array();
                for( unsigned int j = 0; j < outArray.size(); ++j ) {
                    outgoing.push_back(Handle(outArray[j].get_uint64()));
                }
            } else if (name == "truthvalue") {
                tv = JSONToTV(value);
                // Null TV returned on error and usually they'll be
                // an error message already sent to output
                if (tv == NULL) {
                    if (_output.str().size() == 0) {
                        _output << "{\"error\":\"parsing truthvalue\"}" << std::endl;
                        send(_output.str());
                    }
                    return false;
                }
            }

        }
    } catch (std::runtime_error e) {
        // json spirit probably borked at parsing bad javascript
        if (tv) delete tv;
        if (_output.str().size() == 0) {
            _output << "{\"error\":\"parsing json\"}" << std::endl;
            send(_output.str());
        }
        return false;
    }
    if (t == NOTYPE) {
        _output << "{\"error\":\"no type\"}" << std::endl;
        send(_output.str());
        if (tv) delete tv; // remember to clean up TV if it's around
        return false;
    }
    if (tv == NULL) {
        _output << "{\"error\":\"no truthvalue\"}" << std::endl;
        send(_output.str());
        return false;
    }
    Handle h;
    bool exists = false;
    if (classserver().isLink(t) ) {
        if (atomName != "") {
            _output << "{\"error\":\"links can't have name\"}" << std::endl;
            send(_output.str());
            delete tv; // remember to clean up TV if it's around
            return false;
        }
        h = as->getHandle(t, outgoing);
        if (!TLB::isInvalidHandle(h)) exists = true;
        h = as->addLink(t, outgoing, *tv);
    } else {
        h = as->getHandle(t, atomName);
        if (!TLB::isInvalidHandle(h)) exists = true;
        h = as->addNode(t,atomName, *tv);
    }
    delete tv;

    if (TLB::isInvalidHandle(h)) {
        _output << "{\"error\":\"invalid handle returned\"}" << std::endl;
        send(_output.str());
        return false;
    } else {
        json_makeOutput(h,exists);
        send(_output.str());
    }
    return true;
}

void CreateAtomRequest::json_makeOutput(Handle h, bool exists)
{
    if (exists)
        _output << "{\"result\":\"merged\",";
    else
        _output << "{\"result\":\"created\",";
    _output << "\"handle\":" << h.value() << "}" << std::endl;

}

bool CreateAtomRequest::assertJSONTVCorrect(std::string expected,
        std::string actual)
{
    if (expected != actual) {
        _output << "{\"error\":\"expected '" << expected << "' but got '"
            << actual << "', check JSON.\"}" << std::endl;
        send(_output.str());
        return false;
    }
    return true;
}

bool CreateAtomRequest::assertJsonMapContains(const Object& o,
        std::vector<std::string> keys)
{
    std::vector<bool> key_present;
    for (unsigned int i=0; i < keys.size(); ++i)
        key_present.push_back(false);
    // check all strings in keys are in json object
    for( Object::size_type i = 0; i < o.size(); ++i ) {
        // # keys checked should only be small amounts < 5
        // so just iterate.
        unsigned int j;
        for (j = 0; j < keys.size(); j++) {
            if (keys[j] == o[i].name_) {
                key_present[j] = true;
                break;
            }
        }
        if (j == keys.size()) {
            _output << "{\"error\":\"unknown truth value key '" << o[i].name_ <<
                "'\"}" << std::endl;
            send(_output.str());
            return false; // unexpected key
        }
    }
    for (unsigned int i=0; i < keys.size(); ++i) {
        if (!key_present[i]) {
            _output << "{\"error\":\"missing truth value key '" << keys[i] <<
                "'\"}" << std::endl;
            send(_output.str());
            return false;
        }
    }
    return true;
}

TruthValue* CreateAtomRequest::JSONToTV(const Value& v)
{
    TruthValue* tv = NULL;
    const Object& tv_obj = v.get_obj();
    if (tv_obj.size() != 1) return NULL;
    const Pair& pair = tv_obj[0];
    std::string tv_type_str = pair.name_;
    if (tv_type_str == "simple") {
        const Object& stv_obj = pair.value_.get_obj();
        float str, count;
        std::vector<std::string> key_list;
        key_list.push_back("str");
        key_list.push_back("count");
        if (!assertJsonMapContains(stv_obj, key_list)) return NULL;
        for( Object::size_type i = 0; i < stv_obj.size(); ++i ) {
            if (stv_obj[i].name_ == "str")
                str = stv_obj[i].value_.get_real();
            else if (stv_obj[i].name_ == "count")
                count = stv_obj[i].value_.get_real();
        }
        tv = new SimpleTruthValue(str,count);
    } else if (tv_type_str == "count") {
        const Object& stv_obj = pair.value_.get_obj();
        float str, conf, count;
        std::vector<std::string> key_list;
        key_list.push_back("str");
        key_list.push_back("conf");
        key_list.push_back("count");
        if (!assertJsonMapContains(stv_obj, key_list)) return NULL;
        for( Object::size_type i = 0; i < stv_obj.size(); ++i ) {
            if (stv_obj[i].name_ == "str")
                str = stv_obj[i].value_.get_real();
            else if (stv_obj[i].name_ == "count")
                count = stv_obj[i].value_.get_real();
            else if (stv_obj[i].name_ == "conf")
                conf = stv_obj[i].value_.get_real();
        }
        tv = new CountTruthValue(str,conf,count);
    } else if (tv_type_str == "indefinite") {
        //! @todo Allow asymmetric Indefinite TVs
        const Object& stv_obj = pair.value_.get_obj();
        float conf, l, u;
        std::vector<std::string> key_list;
        key_list.push_back("l");
        key_list.push_back("u");
        key_list.push_back("conf");
        if (!assertJsonMapContains(stv_obj, key_list)) return NULL;
        for( Object::size_type i = 0; i < stv_obj.size(); ++i ) {
            if (stv_obj[i].name_ == "l")
                l = stv_obj[i].value_.get_real();
            else if (stv_obj[i].name_ == "u")
                u = stv_obj[i].value_.get_real();
            else if (stv_obj[i].name_ == "conf")
                conf = stv_obj[i].value_.get_real();
        }
        tv = new IndefiniteTruthValue(l, u, conf);
    } else if (tv_type_str == "composite") {
        const Object& stv_obj = pair.value_.get_obj();
        if (stv_obj.size() == 0) return NULL;
        if (!assertJSONTVCorrect("primary",stv_obj[0].name_)) return NULL;
        TruthValue* primary_tv = JSONToTV(stv_obj[0].value_);
        if (primary_tv == NULL) return NULL;
        CompositeTruthValue* ctv = new CompositeTruthValue(*primary_tv,
                NULL_VERSION_HANDLE);
        delete primary_tv;
        for( Object::size_type i = 1; i < stv_obj.size(); ++i ) {
            TruthValue* ctxt_tv = NULL;
            std::string indicatorStr;
            try {
                const Pair& pair = stv_obj[i];
                indicatorStr = pair.name_; // 
                const Array& vharray = pair.value_.get_array();
                // should be [ contextual UUID, {TV} ]
                if (vharray.size() != 2) continue;
                Handle context = Handle(vharray[0].get_uint64());
                ctxt_tv = JSONToTV(vharray[1]);
                if (ctxt_tv == NULL) continue;
                IndicatorType indicator =
                    VersionHandle::strToIndicator(indicatorStr.c_str());
                ctv->setVersionedTV(*ctxt_tv,VersionHandle(indicator,context));
                delete ctxt_tv; // Composite clones TV
            } catch (InvalidParamException& e) {
                _output << "{\"error\":\"bad indicator for version handle: '" <<
                    indicatorStr << "'\"}" << std::endl;
                send(_output.str());
                delete ctv;
                delete ctxt_tv;
                return NULL;
            } catch (std::runtime_error& e) {
                _output << "{\"error\":\"bad json in truth value\"}" << std::endl;
                send(_output.str());
                delete ctv;
                return NULL;
            }
        }
        tv = ctv;
    }
    return tv;

}

