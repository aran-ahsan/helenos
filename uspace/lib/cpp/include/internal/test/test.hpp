/*
 * Copyright (c) 2017 Jaroslav Jindrak
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LIBCPP_TEST_TEST
#define LIBCPP_TEST_TEST

#include <utility>

namespace std::test
{
    class test_suite
    {
        public:
            virtual bool run() = 0;
            virtual const char* name() = 0;

            virtual ~test_suite() = default;

            unsigned int get_failed() const noexcept;

            unsigned int get_succeeded() const noexcept;

        protected:
            void report(bool, const char*);
            void start();
            bool end();

            unsigned int failed_{};
            unsigned int succeeded_{};
            bool ok_{true};

            template<class... Args>
            void test_eq(const char* tname, Args&&... args)
            {
                if (!assert_eq(std::forward<Args>(args)...))
                {
                    report(false, tname);
                    ++failed_;
                    ok_ = false;
                }
                else
                {
                    report(true, tname);
                    ++succeeded_;
                }
            }

            template<class T>
            bool assert_eq(const T& lhs, const T& rhs)
            {
                return lhs == rhs;
            }

            template<class Iterator1, class Iterator2>
            bool assert_eq(Iterator1 first1, Iterator1 last1,
                           Iterator2 first2, Iterator2 last2)
            {
                if ((last1 - first1) != (last2 - first2))
                    return false;

                while (first1 != last1)
                {
                    if (*first1++ != *first2++)
                        return false;
                }

                return true;
            }
    };
}

#endif