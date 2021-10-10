// MIT License
//
// Copyright (c) 2021. Happy GardenPI
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

//
// Created by Antonio Salsi on 16/08/21.
//

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnreachableCallsOfFunction"

#include <hgardenpi-protocol/protocol.hpp>

#include <stdexcept>
#include <memory>
#include <iostream>
using namespace std;

#include <cstring>
#include <cmath>

#include <hgardenpi-protocol/3thparts/libcrc/checksum.h>

#include <hgardenpi-protocol/packages/aggregation.hpp>
#include <hgardenpi-protocol/packages/data.hpp>
#include <hgardenpi-protocol/packages/finish.hpp>
#include <hgardenpi-protocol/packages/station.hpp>
#include <hgardenpi-protocol/packages/synchro.hpp>
#include <hgardenpi-protocol/packages/error.hpp>

namespace hgardenpi::protocol
{
    inline namespace v1
    {

        struct DataTransport
        {
            /**
             * @brief common payload
             */
            uint8_t *payload = nullptr;

            /**
             * @brief common payload ptr
             */
            uint8_t *payloadPtr = nullptr;

            /**
             * @brief common payload length
             */
            uint16_t length = 0;

            /**
             * @brief flag of Head packages
             */
            uint8_t flags = NOT_SET;
        };


        /**
         * @brief initialize Head::Ptr with data
         * @brief data Data container
         * @return a new instance of Head::Ptr semi filled
         */
        static Head::Ptr newHead(DataTransport &data);

        /**
          * Convert a head::Ptr to buffer ready to send
           * @param head head to send
          * @return uint8_t allocated buffer ready to send, to deallocate
          * @throw runtime_exception if something goes wrong
          */
        //static tuple<uint8_t *, size_t> encodeHeadToBuffer(const Head::Ptr &head);

        /**
         * Add data to base Head whit SYN information
         * @param ret vector of Head already converted before this passage
         * @param data to encode
         * @param t pointer to structure
         */
        template<typename T>
        static vector<Head::Ptr> encodeDataToHeads(vector<Head::Ptr> &ret, DataTransport &data, const T *t);

        /**
         * Encode package and split it in more Head if needed
         * @param package package to send, it will be deleted automatically
         * @param additionalFags additional flags to decorate package
         * @return a vector of Head to send
         * @throw runtime_exception if something goes wrong
         */
        vector<Head::Ptr> encodeStart(Package *package, Flags additionalFags);

        /**
         * Add data to base Head whit SYN information
         * @param data to encode
         * @param t pointer to structure
         */
        template<typename T>
        static inline vector<Head::Ptr> encodeRecursive(DataTransport &data, const T *t)
        {
            vector<Head::Ptr> ret;
            return encodeDataToHeads(ret, data, t);
        }

        //enter point
        vector<Buffer> encode(Package *package, Flags additionalFags)
        {
            vector<Buffer> ret;
            for (auto &&head: encodeStart(package, additionalFags))
            {
                if (!head)
                {
                    throw runtime_error("head nullptr");
                }

                auto buf = new(nothrow) uint8_t[5 + head->length];
                if (!buf)
                {
                    throw runtime_error("no memory for ret");
                }
                memset(buf, 0, 5 + head->length);

                buf[0] = (head->version << 0x07) | head->flags;
                buf[1] = head->id;
                buf[2] = head->length;

                uint8_t *ptrPayload = head->payload;
                for (uint8_t i = 0; i < head->length; i++)
                {
                    buf[3 + i] = *ptrPayload;
                    ptrPayload++;
                }

                //calculate size of crc16 and alloc it
                size_t dataLessCrc16Length =
                        sizeof(uint8_t) + //version and flags
                        sizeof(uint8_t) + //id
                        sizeof(uint8_t) + //length
                        (sizeof(uint8_t) * head->length); //payload
                head->crc16 = crc_16(&buf[0], dataLessCrc16Length);

                //fill buffer with crc16
                buf[3 + head->length] = static_cast<uint8_t>((head->crc16 & 0x00FF));
                buf[4 + head->length] = static_cast<uint8_t>((head->crc16 & 0xFF00) >> 0x08);

                ret.emplace_back(buf, 5 + head->length);
            }

            return ret;
        }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
        vector<Head::Ptr> encodeStart(Package *package, Flags additionalFags)
        {
            //check if package is null
            if (package == nullptr)
            {
                throw runtime_error("package null");
            }

            vector<Head::Ptr> ret;
            DataTransport data;

            //check which child package was packaged
            if (auto ptr = dynamic_cast<Aggregation *>(package); ptr) //is Flags::AGG package
                //update flags
                data.flags = AGG | additionalFags;
            else if (auto ptr = dynamic_cast<Data *>(package); ptr) //is Flags::CRT package
                //update flags
                data.flags = DAT | additionalFags;
            else if (dynamic_cast<Finish *>(package)) //is Flags::FIN package
                //add package flag
                data.flags = FIN | additionalFags;
            else if (auto ptr = dynamic_cast<Station *>(package); ptr) //is Flags::STA package
                //update flags
                data.flags = STA | additionalFags;
            else if (auto ptr = dynamic_cast<Error *>(package); ptr) //is Flags::ERR package
                //update flags
                data.flags = ERR | additionalFags;
            else if (auto ptr = dynamic_cast<Synchro *>(package); ptr) //is Flags::SYN package
                //add package flag
                data.flags = SYN | additionalFags;
            else
                throw runtime_error("class not child of Package");

            auto &&[buf, size] = package->serialize();

            //alloc memory
            data.payload = buf.get();

            //set length of package
            data.length = size;

            ret = move(encodeRecursive(data, package));

            return ret;
        }

        template<typename T>
        [[maybe_unused]] static vector<Head::Ptr> encodeDataToHeads(vector<Head::Ptr> &ret, DataTransport &data, const T *t)
        {
            static_assert(is_base_of<Package, T>::value, "T is non subclass of Package");

            //verify length dimension
            if (data.length < HEAD_MAX_PAYLOAD_SIZE)
            {
                //move pointer to filled payload if it's first head, il ret > 0 it means we are in recursion loop
                if (ret.empty())
                {
                    data.payloadPtr = data.payload;
                }

                //create head
                ret.push_back(move(newHead(data)));
            } else if (data.length > 0)
            {
                //check how many heads already build
                if (ret.size() >= HEAD_MAX_PARTIAL)
                {
                    throw runtime_error("data to big, exceed HEAD_MAX_PARTIAL");
                }

                //create data for elaborate in newHead
                DataTransport dataLocal;
                dataLocal.payload = data.payload;
                dataLocal.payloadPtr = dataLocal.payload;
                dataLocal.length = HEAD_MAX_PAYLOAD_SIZE;
                dataLocal.flags = data.flags | PRT;

                //create head
                ret.push_back(move(newHead(dataLocal)));

                //update data for next package
                dataLocal.payloadPtr += HEAD_MAX_PAYLOAD_SIZE;

                //update size for next package
                dataLocal.length = data.length - HEAD_MAX_PAYLOAD_SIZE;

                //create one more head, in recursive mode
                encodeDataToHeads(ret, dataLocal, t);
            }

            //check if vector it's a stack of Head control or not then add FIN if not exist
            if (ret.size() > 1 && !(ret.back()->flags & FIN))
            {
                auto fin = new(nothrow) Finish;
                if (!fin)
                {
                    throw runtime_error("no memory for fin");
                }

                uint8_t flags = NOT_SET;
                if ((data.flags & ACK) == ACK)
                {
                    flags |= ACK;
                }
                if ((data.flags & PRT) == PRT)
                {
                    flags |= PRT;
                }
                auto &&enc = encodeStart(fin, static_cast<Flags>(flags));

                if (!enc.empty())
                {
                    ret.push_back(enc[0]);
                }

                delete fin;
            }

            return ret;
        }


        static Head::Ptr newHead(DataTransport &data)
        {
            if (data.length > HEAD_MAX_PAYLOAD_SIZE)
            {
                throw runtime_error("payload length exceed");
            }

            //alloc heap
            //prepare return head with common information
            Head::Ptr head(new(nothrow) Head{
                    .version = 0,
                    .flags = NOT_SET,
                    .id = 0,
                    .length = 0
            });
            if (head == nullptr)
            {
                throw runtime_error("no memory for head");
            }

            //alloc heap
            head->length = data.length;
            head->payload = new(nothrow) uint8_t[head->length];
            if (!head->payload)
            {
                throw runtime_error("no memory for ret->payload");
            }

            //set payload to 0
            memset(head->payload, 0, head->length);

            head->flags = data.flags;

            //copy data payload to head
            memcpy(head->payload, data.payloadPtr, data.length);

            return head;
        }

#pragma clang diagnostic pop

        Head::Ptr decode(const uint8_t *data)
        {
            //initialize default return value
            Head::Ptr ret(new(nothrow) Head{
                    .version = static_cast<uint8_t>((data[0] & 0x80) >> 0x07),
                    .flags = static_cast<uint8_t>(data[0] & 0x7F),
                    .id = static_cast<uint8_t>(data[1]),
                    .length = static_cast<uint8_t>(data[2])
            });
            if (ret == nullptr)
            {
                throw runtime_error("no memory for head");
            }

            if (ret->version > 1)
            {
                throw runtime_error("version of range");
            }

            //check max init of value
            if (ret->flags > 0xE0)
            {
                throw runtime_error("head flags out of range or more packages set");
            }

            //alloc heap
            ret->payload = new(nothrow) uint8_t[ret->length];
            if (!ret->payload)
            {
                throw runtime_error("no memory for head->payload");
            }

            //set payload to 0
            memset(ret->payload, 0, ret->length);

            //copy payload from data
            memcpy(ret->payload, &data[3], ret->length);

            //copy crc16 from data
            ret->crc16 = static_cast<uint16_t>((data[ret->length + 4] << 0x08) | data[ret->length + 3]);

            //calculate crc16 from data received
            const uint16_t dataLessCrc16Length = ret->length + 3;
            uint16_t crc16 = crc_16(data, dataLessCrc16Length);

            //check crc16 send with that calculate
            if (crc16 != ret->crc16)
            {
                throw runtime_error("crc not match");
            }

            return ret;
        }

        void updateIdToBufferEncoded(Buffer &buffer, uint8_t id)
        {
            buffer.first[2] = id;

            uint16_t crc16Calc = crc_16(buffer.first.get(), buffer.second - 2);

            buffer.first[buffer.second - 2] = static_cast<uint8_t>((crc16Calc & 0x00FF));
            buffer.first[buffer.second - 1] = static_cast<uint8_t>((crc16Calc & 0xFF00) >> 0x08);
        }


    }
}

#pragma clang diagnostic pop