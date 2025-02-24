
//   Copyright 2016-2022 otris software AG
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//
//   This project is hosted at https://github.com/otris

#pragma once

// RapidJSON derives from std::iterator causing the corresponding warnings
#if _MSC_VER >= 1910
#    define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#endif

#include <algorithm>
#include <chrono>
#include <ctype.h>
#include <exception>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef EWS_HAS_OPTIONAL
#    include <optional>
#endif
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#ifdef EWS_HAS_VARIANT
#    include <variant>
#endif
#include "rapidjson/document.h"
#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_print.hpp"

#include <curl/curl.h>
#include <vector>

// Print more detailed error messages, HTTP request/response etc to stderr
#ifdef EWS_ENABLE_VERBOSE
#    include <iostream>
#    include <ostream>
#endif

#include "ews_fwd.hpp"

#ifdef __GNUC__
#    ifdef __clang__
#        ifdef __has_attribute(maybe_unused)
#            define EWS_MAYBE_UNUSED [[maybe_unused]]
#        else
#            define EWS_MAYBE_UNUSED
#        endif
#    else
#        define EWS_MAYBE_UNUSED [[maybe_unused]]
#    endif
#else
#    define EWS_MAYBE_UNUSED
#endif

//! Contains all classes, functions, and enumerations of this library
namespace ews
{

class contact;

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS

#    ifdef EWS_HAS_DEFAULT_TEMPLATE_ARGS_FOR_FUNCTIONS
template <typename ExceptionType = assertion_error>
#    else
template <typename ExceptionType>
#    endif
inline void check(bool expr, const char* msg)
{
    if (!expr)
    {
        throw ExceptionType(msg);
    }
}

#    ifndef EWS_HAS_DEFAULT_TEMPLATE_ARGS_FOR_FUNCTIONS
// Add overload so template argument deduction won't fail on VS 2012
inline void check(bool expr, const char* msg)
{
    check<assertion_error>(expr, msg);
}
#    endif

#endif // EWS_DOXYGEN_SHOULD_SKIP_THIS

namespace internal
{
    //! Helper function for casts for positive values of
    //! an unsigned type to a signed type.
    template <typename TargetType, typename SourceType>
    TargetType numeric_cast(SourceType value)
    {
        if (static_cast<TargetType>(value) >
            std::numeric_limits<TargetType>::max())
        {
            throw std::overflow_error("Cannot convert ");
        }

        return static_cast<TargetType>(value);
    }

    // Scope guard helper.
    class on_scope_exit final
    {
    public:
        template <typename Function> on_scope_exit(Function destructor_function)
        try : func_(std::move(destructor_function))
        {
        }
        catch (std::exception&)
        {
            destructor_function();
        }

#ifdef EWS_HAS_DEFAULT_AND_DELETE
        on_scope_exit() = delete;
        on_scope_exit(const on_scope_exit&) = delete;
        on_scope_exit& operator=(const on_scope_exit&) = delete;
#else
    private:
        on_scope_exit(const on_scope_exit&);            // Never defined
        on_scope_exit& operator=(const on_scope_exit&); // Never defined

    public:
#endif

        ~on_scope_exit()
        {
            if (func_)
            {
                try
                {
                    func_();
                }
                catch (std::exception&)
                {
                    // Swallow
                }
            }
        }

        void release() EWS_NOEXCEPT { func_ = nullptr; }

    private:
        std::function<void(void)> func_;
    };

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
    static_assert(!std::is_copy_constructible<on_scope_exit>::value);
    static_assert(!std::is_copy_assignable<on_scope_exit>::value);
    static_assert(!std::is_move_constructible<on_scope_exit>::value);
    static_assert(!std::is_move_assignable<on_scope_exit>::value);
    static_assert(!std::is_default_constructible<on_scope_exit>::value);
#endif

#ifndef EWS_HAS_OPTIONAL
    // Poor man's std::optional replacement
    template <typename T> class optional final
    {
#    ifdef EWS_HAS_NON_BUGGY_TYPE_TRAITS
        static_assert(std::is_copy_constructible<T>::value,
                      "T needs to be default constructible");
#    endif

    public:
        typedef T value_type;

        optional() : value_set_(false), val_() {}

        template <typename U>
        optional(U&& val) : value_set_(true), val_(std::forward<U>(val))
        {
        }

        optional& operator=(T&& value)
        {
            val_ = std::move(value);
            value_set_ = true;
            return *this;
        }

        bool has_value() const EWS_NOEXCEPT { return value_set_; }

        // Note we throw std::runtime_exception instead of
        // std::bad_optional_access
        const T& value() const
        {
            if (!has_value())
            {
                throw std::runtime_error("Bad ews::internal::optional access");
            }
            return val_;
        }

    private:
        bool value_set_;
        value_type val_;
    };

    template <typename T, typename U>
    inline bool operator==(const optional<T>& opt, const U& value)
    {
        return opt.has_value() ? opt.value() == value : false;
    }
#endif

    namespace base64
    {
        // Following code (everything in base64 namespace) is a slightly
        // modified version of the original implementation from
        // René Nyffenegger available at
        //
        //     https://github.com/ReneNyffenegger/cpp-base64
        //
        // under the zlib License.
        //
        // Copyright © 2004-2017 by René Nyffenegger
        //
        // This source code is provided 'as-is', without any express or implied
        // warranty. In no event will the author be held liable for any damages
        // arising from the use of this software.
        //
        // Permission is granted to anyone to use this software for any purpose,
        // including commercial applications, and to alter it and redistribute
        // it freely, subject to the following restrictions:
        //
        // 1. The origin of this source code must not be misrepresented; you
        //    must not claim that you wrote the original source code. If you use
        //    this source code in a product, an acknowledgment in the product
        //    documentation would be appreciated but is not required.
        //
        // 2. Altered source versions must be plainly marked as such, and must
        //    not be misrepresented as being the original source code.
        //
        // 3. This notice may not be removed or altered from any source
        //    distribution.
        //
        // René Nyffenegger rene.nyffenegger@adp-gmbh.ch

        inline const std::string& valid_chars()
        {
            static const auto chars = std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                                  "abcdefghijklmnopqrstuvwxyz"
                                                  "0123456789+/");
            return chars;
        }

        inline bool is_base64(unsigned char c) EWS_NOEXCEPT
        {
            return isalnum(c) || (c == '+') || (c == '/');
        }

        inline std::string encode(const std::vector<unsigned char>& buf)
        {
            const auto& base64_chars = valid_chars();
            int i = 0;
            int j = 0;
            unsigned char char_array_3[3];
            unsigned char char_array_4[4];
            auto buflen = buf.size();
            auto bufit = begin(buf);
            std::string ret;

            while (buflen--)
            {
                char_array_3[i++] = *(bufit++);
                if (i == 3)
                {
                    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) +
                                      ((char_array_3[1] & 0xf0) >> 4);
                    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) +
                                      ((char_array_3[2] & 0xc0) >> 6);
                    char_array_4[3] = char_array_3[2] & 0x3f;

                    for (i = 0; (i < 4); i++)
                    {
                        ret += base64_chars[char_array_4[i]];
                    }
                    i = 0;
                }
            }

            if (i)
            {
                for (j = i; j < 3; j++)
                {
                    char_array_3[j] = '\0';
                }

                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) +
                                  ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) +
                                  ((char_array_3[2] & 0xc0) >> 6);

                for (j = 0; (j < i + 1); j++)
                {
                    ret += base64_chars[char_array_4[j]];
                }

                while ((i++ < 3))
                {
                    ret += '=';
                }
            }

            return ret;
        }

        inline std::vector<unsigned char>
        decode(const std::string& encoded_string)
        {
            const auto& base64_chars = valid_chars();
            auto in_len = encoded_string.size();
            int i = 0;
            int j = 0;
            int in = 0;
            unsigned char char_array_4[4];
            unsigned char char_array_3[3];
            std::vector<unsigned char> ret;

            while (in_len-- && (encoded_string[in] != '=') &&
                   is_base64(encoded_string[in]))
            {
                char_array_4[i++] = encoded_string[in];
                in++;

                if (i == 4)
                {
                    for (i = 0; i < 4; i++)
                    {
                        char_array_4[i] = static_cast<unsigned char>(
                            base64_chars.find(char_array_4[i]));
                    }

                    char_array_3[0] = (char_array_4[0] << 2) +
                                      ((char_array_4[1] & 0x30) >> 4);
                    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) +
                                      ((char_array_4[2] & 0x3c) >> 2);
                    char_array_3[2] =
                        ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

                    for (i = 0; (i < 3); i++)
                    {
                        ret.push_back(char_array_3[i]);
                    }
                    i = 0;
                }
            }

            if (i)
            {
                for (j = 0; j < i; j++)
                {
                    char_array_4[j] = static_cast<unsigned char>(
                        base64_chars.find(char_array_4[j]));
                }

                char_array_3[0] =
                    (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                char_array_3[1] = ((char_array_4[1] & 0xf) << 4) +
                                  ((char_array_4[2] & 0x3c) >> 2);

                for (j = 0; (j < i - 1); j++)
                {
                    ret.push_back(char_array_3[j]);
                }
            }

            return ret;
        }
    } // namespace base64

    template <typename T>
    inline bool points_within_array(T* p, T* begin, T* end)
    {
        // Not 100% sure if this works in each and every case
        return std::greater_equal<T*>()(p, begin) && std::less<T*>()(p, end);
    }

    // Forward declarations
    class http_request;
} // namespace internal

//! The ResponseClass attribute of a ResponseMessage
enum class response_class
{
    //! An error has occurred
    error,

    //! Everything went fine
    success,

    //! Something strange but not fatal happened
    warning
};

//! Base-class for all exceptions thrown by this library
class exception : public std::runtime_error
{
public:
    explicit exception(const std::string& what) : std::runtime_error(what) {}
    explicit exception(const char* what) : std::runtime_error(what) {}
};

//! Raised when an assertion fails
class assertion_error final : public exception
{
public:
    explicit assertion_error(const std::string& what) : exception(what) {}
    explicit assertion_error(const char* what) : exception(what) {}
};

//! Raised when a response from a server could not be parsed
class xml_parse_error final : public exception
{
public:
    explicit xml_parse_error(const std::string& what) : exception(what) {}
    explicit xml_parse_error(const char* what) : exception(what) {}

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
    static std::string error_message_from(const rapidxml::parse_error& exc,
                                          const std::vector<char>& xml)
    {
        using internal::points_within_array;

        const auto what = std::string(exc.what());
        const auto* where = exc.where<char>();

        if ((where == nullptr) || (*where == '\0'))
        {
            return what;
        }

        std::string msg = what;
        try
        {
            const auto start = &xml[0];
            if (points_within_array(where, start, (start + xml.size() + 1)))
            {
                enum {column_width = 79};
                const auto idx =
                    static_cast<size_t>(std::distance(start, where));

                auto doc = std::string(start, xml.size());
                auto lineno = 1U;
                auto charno = 0U;
                std::replace_if(
                    begin(doc), end(doc),
                    [&](char c) -> bool {
                        charno++;
                        if (c == '\n')
                        {
                            if (charno < idx)
                            {
                                lineno++;
                            }
                            return true;
                        }
                        return false;
                    },
                    ' ');

                // 0-termini probably got replaced by xml_document::parse
                std::replace(begin(doc), end(doc), '\0', '>');

                // Chop off last '>'
                doc = std::string(doc.data(), doc.length() - 1);

                // Construct message
                msg = "in line " + std::to_string(lineno) + ":\n";
                msg += what + '\n';
                const auto pr = shorten(doc, idx, column_width);
                const auto line = pr.first;
                const auto line_index = pr.second;
                msg += line + '\n';

                auto squiggle = std::string(column_width, ' ');
                squiggle[line_index] = '~';
                squiggle = remove_trailing_whitespace(squiggle);
                msg += squiggle + '\n';
            }
        }
        catch (std::exception&)
        {
        }
        return msg;
    }
#endif // EWS_DOXYGEN_SHOULD_SKIP_THIS

private:
    static std::string remove_trailing_whitespace(const std::string& str)
    {
        static const auto whitespace = std::string(" \t");
        const auto end = str.find_last_not_of(whitespace);
        return str.substr(0, end + 1);
    }

    static std::pair<std::string, size_t> shorten(const std::string& str,
                                                  size_t at, size_t columns)
    {
        at = std::min(at, str.length());
        if (str.length() < columns)
        {
            return std::make_pair(str, at);
        }

        const auto start = std::max(at - (columns / 2), static_cast<size_t>(0));
        const auto end = std::min(at + (columns / 2), str.length());
        check((start < end), "Expected start to be before end");
        std::string line;
        std::copy(&str[start], &str[end], std::back_inserter(line));
        const auto line_index = columns / 2;
        return std::make_pair(line, line_index);
    }
};

//! \brief Response code enum describes status information about a request
enum class response_code
{
    //! No error occurred for the request.
    no_error,

    //! This error occurs when the calling account does not have the rights
    //! to perform the requested action.
    error_access_denied,

    //! This error is for internal use only. This error is not returned.
    error_access_mode_specified,

    //! This error occurs when the account in question has been disabled.
    error_account_disabled,

    //! This error occurs when a list with added delegates cannot be saved.
    error_add_delegates_failed,

    //! This error occurs when the address space record, or Domain Name
    //! System (DNS) domain name, for cross-forest availability could not be
    //! found in the Active Directory database.
    error_address_space_not_found,

    //! This error occurs when the operation failed because of communication
    //! problems with Active Directory Domain Services (AD DS).
    error_ad_operation,

    //! This error is returned when a ResolveNames operation request
    //! specifies a name that is not valid.
    error_ad_session_filter,

    //! This error occurs when AD DS is unavailable. Try your request again
    //! later.
    error_ad_unavailable,

    //! This error indicates that the **AffectedTaskOccurrences** attribute
    //! was not specified. When the DeleteItem element is used to delete at
    //! least one item that is a task, and regardless of whether that task
    //! is recurring or not, the **AffectedTaskOccurrences** attribute has
    //! to be specified so that DeleteItem can determine whether to delete
    //! the current occurrence or the entire series.
    error_affected_task_occurrences_required,

    //! Indicates an error in archive folder path creation.
    error_archive_folder_path_creation,

    //! Indicates that the archive mailbox was not enabled.
    error_archive_mailbox_not_enabled,

    //! Specifies that an attempt was made to create an item with more than
    //! 10 nested attachments. This value was introduced in Exchange Server
    //! 2010 Service Pack 2 (SP2).
    error_attachment_nest_level_limit_exceeded,

    //! The CreateAttachment element returns this error if an attempt is
    //! made to create an attachment with size exceeding Int32.MaxValue, in
    //! bytes.
    //!
    //! The GetAttachment element returns this error if an attempt to
    //! retrieve an existing attachment with size exceeding Int32.MaxValue,
    //! in bytes.
    error_attachment_size_limit_exceeded,

    //! This error indicates that Exchange Web Services tried to determine
    //! the location of a cross-forest computer that is running Exchange
    //! 2010 that has the Client Access server role installed by using the
    //! Autodiscover service, but the call to the Autodiscover service
    //! failed.
    error_auto_discover_failed,

    //! This error indicates that the availability configuration information
    //! for the local forest is missing from AD DS.
    error_availability_config_not_found,

    //! This error indicates that an exception occurred while processing an
    //! item and that exception is likely to occur for the items that
    //! follow. Requests may include multiple items; for example, a GetItem
    //! operation request might include multiple identifiers. In general,
    //! items are processed one at a time. If an exception occurs while
    //! processing an item and that exception is likely to occur for the
    //! items that follow, items that follow will not be processed.
    //!
    //! The following are examples of errors that will stop processing for
    //! items that follow:
    //!
    //! \li ErrorAccessDenied
    //! \li ErrorAccountDisabled
    //! \li ErrorADUnavailable
    //! \li ErrorADOperation
    //! \li ErrorConnectionFailed
    //! \li ErrorMailboxStoreUnavailable
    //! \li ErrorMailboxMoveInProgress
    //! \li ErrorPasswordChangeRequired
    //! \li ErrorPasswordExpired
    //! \li ErrorQuotaExceeded
    //! \li ErrorInsufficientResources
    error_batch_processing_stopped,

    //! This error occurs when an attempt is made to move or copy an
    //! occurrence of a recurring calendar item.
    error_calendar_cannot_move_or_copy_occurrence,

    //! This error occurs when an attempt is made to update a calendar item
    //! that is located in the Deleted Items folder and when meeting updates
    //! or cancellations are to be sent according to the value of the
    //! SendMeetingInvitationsOrCancellations attribute. The following
    //! are the possible values for this attribute:
    //!
    //! \li SendToAllAndSaveCopy
    //! \li SendToChangedAndSaveCopy
    //! \li SendOnlyToAll
    //! \li SendOnlyToChanged
    //! However, such an update is allowed only when the value of this
    //! attribute is set to SendToNone.
    error_calendar_cannot_update_deleted_item,

    //! This error occurs when the UpdateItem, GetItem, DeleteItem,
    //! MoveItem, CopyItem, or SendItem operation is called and the ID that
    //! was specified is not an occurrence ID of any recurring calendar
    //! item.
    error_calendar_cannot_use_id_for_occurrence_id,

    //! This error occurs when the UpdateItem, GetItem, DeleteItem,
    //! MoveItem, CopyItem, or SendItem operation is called and the ID that
    //! was specified is not an ID of any recurring master item.
    error_calendar_cannot_use_id_for_recurring_master_id,

    //! This error occurs during a CreateItem or UpdateItem operation when a
    //! calendar item duration is longer than the maximum allowed, which is
    //! currently 5 years.
    error_calendar_duration_is_too_long,

    //! This error occurs when a calendar End time is set to the same time
    //! or after the Start time.
    error_calendar_end_date_is_earlier_than_start_date,

    //! This error occurs when the specified folder for a FindItem operation
    //! with a CalendarView element is not of calendar folder type.
    error_calendar_folder_is_invalid_for_calendar_view,

    //! This response code is not used.
    error_calendar_invalid_attribute_value,

    //! This error occurs during a CreateItem or UpdateItem operation when
    //! invalid values of Day, WeekendDay, and Weekday are used to define
    //! the time change pattern.
    error_calendar_invalid_day_for_time_change_pattern,

    //! This error occurs during a CreateItem or UpdateItem operation when
    //! invalid values of Day, WeekDay, and WeekendDay are used to specify
    //! the weekly recurrence.
    error_calendar_invalid_day_for_weekly_recurrence,

    //! This error occurs when the state of a calendar item recurrence
    //! binary large object (BLOB) in the Exchange store is invalid.
    error_calendar_invalid_property_state,

    //! This response code is not used.
    error_calendar_invalid_property_value,

    //! This error occurs when the specified recurrence cannot be created.
    error_calendar_invalid_recurrence,

    //! This error occurs when an invalid time zone is encountered.
    error_calendar_invalid_time_zone,

    //! This error Indicates that a calendar item has been canceled.
    error_calendar_is_cancelled_for_accept,

    //! This error indicates that a calendar item has been canceled.
    error_calendar_is_cancelled_for_decline,

    //! This error indicates that a calendar item has been canceled.
    error_calendar_is_cancelled_for_remove,

    //! This error indicates that a calendar item has been canceled.
    error_calendar_is_cancelled_for_tentative,

    //! This error indicates that the AcceptItem element is invalid for a
    //! calendar item or meeting request in a delegated scenario.
    error_calendar_is_delegated_for_accept,

    //! This error indicates that the DeclineItem element is invalid for a
    //! calendar item or meeting request in a delegated scenario.
    error_calendar_is_delegated_for_decline,

    //! This error indicates that the RemoveItem element is invalid for a
    //! meeting cancellation in a delegated scenario.
    error_calendar_is_delegated_for_remove,

    //! This error indicates that the TentativelyAcceptItem element is
    //! invalid for a calendar item or meeting request in a delegated
    //! scenario.
    error_calendar_is_delegated_for_tentative,

    //! This error indicates that the operation (currently CancelItem) on
    //! the calendar item is not valid for an attendee. Only the meeting
    //! organizer can cancel the meeting.
    error_calendar_is_not_organizer,

    //! This error indicates that AcceptItem is invalid for the organizer’s
    //! calendar item.
    error_calendar_is_organizer_for_accept,

    //! This error indicates that DeclineItem is invalid for the organizer’s
    //! calendar item.
    error_calendar_is_organizer_for_decline,

    //! This error indicates that RemoveItem is invalid for the organizer’s
    //! calendar item. To remove a meeting from the calendar, the organizer
    //! must use CancelCalendarItem.
    error_calendar_is_organizer_for_remove,

    //! This error indicates that TentativelyAcceptItem is invalid for the
    //! organizer’s calendar item.
    error_calendar_is_organizer_for_tentative,

    //! This error indicates that a meeting request is out-of-date and
    //! cannot be updated.
    error_calendar_meeting_request_is_out_of_date,

    //! This error indicates that the occurrence index does not point to an
    //! occurrence within the current recurrence. For example, if your
    //! recurrence pattern defines a set of three meeting occurrences and
    //! you try to access the fifth occurrence, this response code will
    //! result.
    error_calendar_occurrence_index_is_out_of_recurrence_range,

    //! This error indicates that any operation on a deleted occurrence
    //! (addressed via recurring master ID and occurrence index) is invalid.
    error_calendar_occurrence_is_deleted_from_recurrence,

    //! This error is reported on CreateItem and UpdateItem operations for
    //! calendar items or task recurrence properties when the property value
    //! is out of range. For example, specifying the fifteenth week of the
    //! month will result in this response code.
    error_calendar_out_of_range,

    //! This error occurs when Start to End range for the CalendarView
    //! element is more than the maximum allowed, currently 2 years.
    error_calendar_view_range_too_big,

    //! This error indicates that the requesting account is not a valid
    //! account in the directory database.
    error_caller_is_invalid_ad_account,

    //! Indicates that an attempt was made to archive a calendar contact
    //! task folder.
    error_cannot_archive_calendar_contact_task_folder_exception,

    //! Indicates that an attempt was made to archive items in public
    //! folders.
    error_cannot_archive_items_in_public_folders,

    //! Indicates that attempt was made to archive items in the archive
    //! mailbox.
    error_cannot_archive_items_in_archive_mailbox,

    //! This error occurs when a calendar item is being created and the
    //! **SavedItemFolderId** attribute refers to a non-calendar folder.
    error_cannot_create_calendar_item_in_non_calendar_folder,

    //! This error occurs when a contact is being created and the
    //! **SavedItemFolderId** attribute refers to a non-contact folder.
    error_cannot_create_contact_in_non_contact_folder,

    //! This error indicates that a post item cannot be created in a folder
    //! other than a mail folder, such as Calendar, Contact, Tasks, Notes,
    //! and so on.
    error_cannot_create_post_item_in_non_mail_folder,

    //! This error occurs when a task is being created and the
    //! **SavedItemFolderId** attribute refers to a non-task folder.
    error_cannot_create_task_in_non_task_folder,

    //! This error occurs when the item or folder to delete cannot be
    //! deleted.
    error_cannot_delete_object,

    //! The DeleteItem operation returns this error when it fails to delete
    //! the current occurrence of a recurring task. This can only happen if
    //! the AffectedTaskOccurrences attribute has been set to
    //! SpecifiedOccurrenceOnly.
    error_cannot_delete_task_occurrence,

    //! Indicates that an attempt was made to disable a mandatory
    //! extension.
    error_cannot_disable_mandatory_extension,

    //! This error must be returned when the server cannot empty a folder.
    error_cannot_empty_folder,

    //! Indicates that the source folder path could not be retrieved.
    error_cannot_get_source_folder_path,

    //! Specifies that the server could not retrieve the external URL for
    //! Outlook Web App Options.
    error_cannot_get_external_ecp_url,

    //! The GetAttachment operation returns this error if it cannot retrieve
    //! the body of a file attachment.
    error_cannot_open_file_attachment,

    //! This error indicates that the caller tried to set calendar
    //! permissions on a non-calendar folder.
    error_cannot_set_calendar_permission_on_non_calendar_folder,

    //! This error indicates that the caller tried to set non-calendar
    //! permissions on a calendar folder.
    error_cannot_set_non_calendar_permission_on_calendar_folder,

    //! This error indicates that you cannot set unknown permissions in a
    //! permissions set.
    error_cannot_set_permission_unknown_entries,

    //! Indicates that an attempt was made to specify the search folder as
    //! the source folder.
    error_cannot_specify_search_folder_as_source_folder,

    //! This error occurs when a request that requires an item identifier is
    //! given a folder identifier.
    error_cannot_use_folder_id_for_item_id,

    //! This error occurs when a request that requires a folder identifier
    //! is given an item identifier.
    error_cannot_use_item_id_for_folder_id,

    //! This response code has been replaced by
    //! ErrorChangeKeyRequiredForWriteOperations
    error_change_key_required,

    //! This error is returned when the change key for an item is missing or
    //! stale. For SendItem, UpdateItem, and UpdateFolder operations, the
    //! caller must pass in a correct and current change key for the item.
    //! Note that this is the case with UpdateItem even when conflict
    //! resolution is set to always overwrite.
    error_change_key_required_for_write_operations,

    //! Specifies that the client was disconnected.
    error_client_disconnected,

    //! This error is intended for internal use only.
    error_client_intent_invalid_state_definition,

    //! This error is intended for internal use only.
    error_client_intent_not_found,

    //! This error occurs when Exchange Web Services cannot connect to the
    //! mailbox.
    error_connection_failed,

    //! This error indicates that the property that was inspected for a
    //! Contains filter is not a string type.
    error_contains_filter_wrong_type,

    //! The GetItem operation returns this error when Exchange Web Services
    //! is unable to retrieve the MIME content for the item requested. The
    //! CreateItem operation returns this error when Exchange Web Services
    //! is unable to create the item from the supplied MIME content. Usually
    //! this is an indication that the item property is corrupted or
    //! truncated.
    error_content_conversion_failed,

    //! This error occurs when a search request is made using the
    //! QueryString option and content indexing is not enabled for the
    //! target mailbox.
    error_content_indexing_not_enabled,

    //! This error occurs when the data is corrupted and cannot be
    //! processed.
    error_corrupt_data,

    //! This error occurs when the caller does not have permission to create
    //! the item.
    error_create_item_access_denied,

    //! This error occurs when one or more of the managed folders that were
    //! specified in the CreateManagedFolder operation request failed to be
    //! created. Search for each folder to determine which folders were
    //! created and which folders do not exist.
    error_create_managed_folder_partial_completion,

    //! This error occurs when the calling account does not have the
    //! permissions required to create the subfolder.
    error_create_subfolder_access_denied,

    //! This error occurs when an attempt is made to move an item or folder
    //! from one mailbox to another. If the source mailbox and destination
    //! mailbox are different, you will get this error.
    error_cross_mailbox_move_copy,

    //! This error indicates that the request is not allowed because the
    //! Client Access server that should service the request is in a
    //! different site.
    error_cross_site_request,

    //! This error can occur in the following scenarios:
    //!
    //! \li An attempt is made to access or write a property on an item and
    //! the property value is too large.
    //! \li The base64 encoded MIME content length within the request XML
    //! exceeds the limit.
    //! \li The size of the body of an existing item body exceeds the limit.
    //! \li The consumer tries to set an HTML or text body whose length (or
    //! combined length in the case of append) exceeds the limit.
    error_data_size_limit_exceeded,

    //! This error occurs when the underlying data provider fails to
    //! complete the operation.
    error_data_source_operation,

    //! This error occurs in an AddDelegate operation when the specified
    //! user already exists in the list of delegates.
    error_delegate_already_exists,

    //! This error occurs in an AddDelegate operation when the specified
    //! user to be added is the owner of the mailbox.
    error_delegate_cannot_add_owner,

    //! This error occurs in a GetDelegate operation when either there is no
    //! delegate information on the local FreeBusy message or no Active
    //! Directory public delegate (no "public delegate" or no "Send On
    //! Behalf" entry in AD DS).
    error_delegate_missing_configuration,

    //! This error occurs when a specified user cannot be mapped to a user
    //! in AD DS.
    error_delegate_no_user,

    //! This error occurs in the AddDelegate operation when an added
    //! delegate user is not valid.
    error_delegate_validation_failed,

    //! This error occurs when an attempt is made to delete a distinguished
    //! folder.
    error_delete_distinguished_folder,

    //! This response code is not used.
    error_delete_items_failed,

    //! This error is intended for internal use only.
    error_delete_unified_messaging_prompt_failed,

    //! This error indicates that a distinguished user ID is not valid for
    //! the operation. DistinguishedUserType should not be present in
    //! the request.
    error_distinguished_user_not_supported,

    //! This error indicates that a request distribution list member does
    //! not exist in the distribution list.
    error_distribution_list_member_not_exist,

    //! This error occurs when duplicate folder names are specified within
    //! the FolderNames element of the CreateManagedFolder operation
    //! request.
    error_duplicate_input_folder_names,

    //! This error indicates that a duplicate user ID has been found in a
    //! permission set, either Default or Anonymous are set more than once,
    //! or there are duplicate SIDs or recipients.
    error_duplicate_user_ids_specified,

    //! This error occurs when a request attempts to create/update the
    //! search parameters of a search folder. For example, this can occur
    //! when a search folder is created in the mailbox but the search folder
    //! is directed to look in another mailbox.
    error_email_address_mismatch,

    //! This error occurs when the event that is associated with a watermark
    //! is deleted before the event is returned. When this error is
    //! returned, the subscription is also deleted.
    error_event_not_found,

    //! This error indicates that there are more concurrent requests
    //! against the server than are allowed by a user's policy.
    error_exceeded_connection_count,

    //! This error indicates that a user's throttling policy maximum
    //! subscription count has been exceeded.
    error_exceeded_subscription_count,

    //! This error indicates that a search operation call has exceeded the
    //! total number of items that can be returned.
    error_exceeded_find_count_limit,

    //! This error occurs if the GetEvents operation is called as a
    //! subscription is being deleted because it has expired.
    error_expired_subscription,

    //! Indicates that the extension was not found.
    error_extension_not_found,

    //! This error occurs when the folder is corrupted and cannot be saved.
    error_folder_corrupt,

    //! This error occurs when an attempt is made to create a folder that
    //! has the same name as another folder in the same parent. Duplicate
    //! folder names are not allowed.
    error_folder_exists,

    //! This error indicates that the folder ID that was specified does not
    //! correspond to a valid folder, or that the delegate does not have
    //! permission to access the folder.
    error_folder_not_found,

    //! This error indicates that the requested property could not be
    //! retrieved. This does not indicate that the property does not exist,
    //! but that the property was corrupted in some way so that the
    //! retrieval failed.
    error_folder_property_request_failed,

    //! This error indicates that the folder could not be created or updated
    //! because of an invalid state.
    error_folder_save,

    //! This error indicates that the folder could not be created or updated
    //! because of an invalid state.
    error_folder_save_failed,

    //! This error indicates that the folder could not be created or updated
    //! because of invalid property values. The response code lists which
    //! properties caused the problem.
    error_folder_save_property_error,

    //! This error indicates that the maximum group member count has been
    //! reached for obtaining free/busy information for a distribution list.
    error_free_busy_dl_limit_reached,

    //! This error is returned when free/busy information cannot be
    //! retrieved because of an intervening failure.
    error_free_busy_generation_failed,

    //! This response code is not used.
    error_get_server_security_descriptor_failed,

    //! This error is returned when new instant messaging (IM) contacts
    //! cannot be added because the maximum number of contacts has been
    //! reached. This error was introduced in Exchange Server 2013.
    error_im_contact_limit_reached,

    //! This error is returned when an attempt is made to add a group
    //! display name when an existing group already has the same display
    //! name. This error was introduced in Exchange 2013.
    error_im_group_display_name_already_exists,

    //! This error is returned when new IM groups cannot be added because
    //! the maximum number of groups has been reached. This error was
    //! introduced in Exchange 2013.
    error_im_group_limit_reached,

    //! The error is returned in the server-to-server authorization case for
    //! Exchange Impersonation when the caller does not have the proper
    //! rights to impersonate the specific user in question. This error maps
    //! to the ms-Exch-EPI-May-Impersonate extended Active Directory right.
    error_impersonate_user_denied,

    //! This error is returned in the server-to-server authorization for
    //! Exchange Impersonation when the caller does not have the proper
    //! rights to impersonate through the Client Access server that they are
    //! making the request against. This maps to the ms-Exch-EPI-
    //! Impersonation extended Active Directory right.
    error_impersonation_denied,

    //! This error indicates that there was an unexpected error when an
    //! attempt was made to perform server-to-server authentication. This
    //! response code typically indicates either that the service account
    //! that is running the Exchange Web Services application pool is
    //! configured incorrectly, that Exchange Web Services cannot talk to
    //! the directory, or that a trust between forests is not correctly
    //! configured.
    error_impersonation_failed,

    //! This error indicates that the request was valid for the current
    //! Exchange Server version but was invalid for the request server
    //! version that was specified.
    error_incorrect_schema_version,

    //! This error indicates that each change description in the UpdateItem
    //! or UpdateFolder elements must list only one property to update.
    error_incorrect_update_property_count,

    //! This error occurs when the request contains too many attendees to
    //! resolve. By default, the maximum number of attendees to resolve is
    //! one hundred.
    error_individual_mailbox_limit_reached,

    //! This error occurs when the mailbox server is overloaded. Try your
    //! request again later.
    error_insufficient_resources,

    //! This error indicates that Exchange Web Services encountered an error
    //! that it could not recover from, and a more specific response code
    //! that is associated with the error that occurred does not exist.
    error_internal_server_error,

    //! This error indicates that an internal server error occurred and that
    //! you should try your request again later.
    error_internal_server_transient_error,

    //! This error indicates that the level of access that the caller has on
    //! the free/busy data is invalid.
    error_invalid_access_level,

    //! This error indicates an error caused by all invalid arguments passed
    //! to the GetMessageTrackingReport operation. This error is returned in
    //! the following scenarios: The user specified in the sending-as
    //! parameter does not exist in the directory; the user specified in the
    //! sending-as parameter is not unique in the directory; the sending-as
    //! address is empty; the sending-as address is not a valid e-mail
    //! address.
    error_invalid_argument,

    //! This error is returned by the GetAttachment operation or the
    //! DeleteAttachment operation when an attachment that corresponds to
    //! the specified ID is not found.
    error_invalid_attachment_id,

    //! This error occurs when you try to bind to an existing search folder
    //! by using a complex attachment table restriction. Exchange Web
    //! Services only supports simple contains filters against the
    //! attachment table. If you try to bind to an existing search folder
    //! that has a more complex attachment table restriction (a subfilter),
    //! Exchange Web Services cannot render the XML for that filter and
    //! returns this response code. Note that you can still call the
    //! GetFolder operation on the folder, but do not request the
    //! SearchParameters element.
    error_invalid_attachment_subfilter,

    //! This error occurs when you try to bind to an existing search folder
    //! by using a complex attachment table restriction. Exchange Web
    //! Services only supports simple contains filters against the
    //! attachment table. If you try to bind to an existing search folder
    //! that has a more complex attachment table restriction, Exchange Web
    //! Services cannot render the XML for that filter. In this case, the
    //! attachment subfilter contains a text filter, but it is not looking
    //! at the attachment display name. Note that you can still call the
    //! GetFolder operation on the folder, but do not request the
    //! SearchParameters element.
    error_invalid_attachment_subfilter_text_filter,

    //! This error indicates that the authorization procedure for the
    //! requestor failed.
    error_invalid_authorization_context,

    //! This error occurs when a consumer passes in a folder or item
    //! identifier with a change key that cannot be parsed. For example,
    //! this could be invalid base64 content or an empty string.
    error_invalid_change_key,

    //! This error indicates that there was an internal error when an
    //! attempt was made to resolve the identity of the caller.
    error_invalid_client_security_context,

    //! This error is returned when an attempt is made to set the
    //! CompleteDate element value of a task to a time in the future. When
    //! it is converted to the local time of the Client Access server, the
    //! CompleteDate of a task cannot be set to a value that is later than
    //! the local time on the Client Access server.
    error_invalid_complete_date,

    //! This error indicates that an invalid e-mail address was provided for
    //! a contact.
    error_invalid_contact_email_address,

    //! This error indicates that an invalid e-mail index value was provided
    //! for an e-mail entry.
    error_invalid_contact_email_index,

    //! This error occurs when the credentials that are used to proxy a
    //! request to a different directory service forest fail authentication.
    error_invalid_cross_forest_credentials,

    //! This error indicates that the specified folder permissions are
    //! invalid.
    error_invalid_delegate_permission,

    //! This error indicates that the specified delegate user ID is invalid.
    error_invalid_delegate_user_id,

    //! This error occurs during Exchange Impersonation when a caller does
    //! not specify a UPN, an e-mail address, or a user SID. This will also
    //! occur if the caller specifies one or more of those and the values
    //! are empty.
    error_invalid_exchange_impersonation_header_data,

    //! This error occurs when the bitmask that was passed into an Excludes
    //! element restriction is unable to be parsed.
    error_invalid_excludes_restriction,

    //! This response code is not used.
    error_invalid_expression_type_for_sub_filter,

    //! This error occurs when the following events take place:
    //!
    //! \li The caller tries to use an extended property that is not
    //! supported by Exchange Web Services.
    //! \li The caller passes in an invalid combination of attribute values
    //! for an extended property. This also occurs if no attributes are
    //! passed. Only certain combinations are allowed.
    error_invalid_extended_property,

    //! This error occurs when the value section of an extended property
    //! does not match the type of the property; for example, setting an
    //! extended property that has PropertyType="String" to an array of
    //! integers will result in this error. Any string representation that
    //! is not coercible into the type that is specified for the extended
    //! property will give this error.
    error_invalid_extended_property_value,

    //! This error indicates that the sharing invitation sender did not
    //! create the sharing invitation metadata.
    error_invalid_external_sharing_initiator,

    //! This error indicates that a sharing message is not intended for the
    //! caller.
    error_invalid_external_sharing_subscriber,

    //! This error indicates that the requestor's organization federation
    //! objects are not correctly configured.
    error_invalid_federated_organization_id,

    //! This error occurs when the folder ID is corrupt.
    error_invalid_folder_id,

    //! This error indicates that the specified folder type is invalid for
    //! the current operation. For example, you cannot create a Search
    //! folder in a public folder.
    error_invalid_folder_type_for_operation,

    //! This error occurs in fractional paging when the user has specified
    //! one of the following:
    //!
    //! \li A numerator that is greater than the denominator
    //! \li A numerator that is less than zero
    //! \li A denominator that is less than or equal to zero
    error_invalid_fractional_paging_parameters,

    //! This error indicates that the DataType and ShareFolderId elements
    //! are both present in a request.
    error_invalid_get_sharing_folder_request,

    //! This error occurs when the GetUserAvailability operation is called
    //! with a FreeBusyViewType of None.
    error_invalid_free_busy_view_type,

    //! This error indicates that the ID and/or change key is malformed.
    error_invalid_id,

    //! This error occurs when the caller specifies an Id attribute that is
    //! empty.
    error_invalid_id_empty,

    //! This error occurs when the item can't be liked. Versions of Exchange
    //! starting with build number 15.00.0913.09 include this value.
    error_invalid_like_request,

    //! This error occurs when the caller specifies an Id attribute that is
    //! malformed.
    error_invalid_id_malformed,

    //! This error indicates that a folder or item ID that is using the
    //! Exchange 2007 format was specified for a request with a version of
    //! Exchange 2007 SP1 or later. You cannot use Exchange 2007 IDs in
    //! Exchange 2007 SP1 or later requests. You have to use the ConvertId
    //! operation to convert them first.
    error_invalid_id_malformed_ews_legacy_id_format,

    //! This error occurs when the caller specifies an Id attribute that is
    //! too long.
    error_invalid_id_moniker_too_long,

    //! This error is returned whenever an ID that is not an item attachment
    //! ID is passed to a Web service method that expects an attachment ID.
    error_invalid_id_not_an_item_attachment_id,

    //! This error occurs when a contact in your mailbox is corrupt.
    error_invalid_id_returned_by_resolve_names,

    //! This error occurs when the caller specifies an Id attribute that is
    //! too long.
    error_invalid_id_store_object_id_too_long,

    //! This error is returned when the attachment hierarchy on an item
    //! exceeds the maximum of 255 levels deep.
    error_invalid_id_too_many_attachment_levels,

    //! This response code is not used.
    error_invalid_id_xml,

    //! This error is returned when the specified IM contact identifier does
    //! not represent a valid identifier. This error was introduced in
    //! Exchange 2013.
    error_invalid_im_contact_id,

    //! This error is returned when the specified IM distribution group SMTP
    //! address identifier does not represent a valid identifier. This error
    //! was introduced in Exchange 2013.
    error_invalid_im_distribution_group_smtp_address,

    //! This error is returned when the specified IM group identifier does
    //! not represent a valid identifier. This error was introduced in
    //! Exchange 2013.
    error_invalid_im_group_id,

    //! This error occurs if the offset for indexed paging is negative.
    error_invalid_indexed_paging_parameters,

    //! This response code is not used.
    error_invalid_internet_header_child_nodes,

    //! Indicates that the item was invalid for an ArchiveItem operation.
    error_invalid_item_for_operation_archive_item,

    //! This error occurs when an attempt is made to use an AcceptItem
    //! response object for an item type other than a meeting request or a
    //! calendar item, or when an attempt is made to accept a calendar item
    //! occurrence that is in the Deleted Items folder.
    error_invalid_item_for_operation_accept_item,

    //! This error occurs when an attempt is made to use a CancelItem
    //! response object on an item type other than a calendar item.
    error_invalid_item_for_operation_cancel_item,

    //! This error is returned when an attempt is made to create an item
    //! attachment of an unsupported type.
    //!
    //! Supported item types for item attachments include the following
    //! objects:
    //!
    //! \li Item
    //! \li Message
    //! \li CalendarItem
    //! \li Task
    //! \li Contact
    //!
    //! For example, if you try to create a MeetingMessage attachment, you
    //! will encounter this response code.
    error_invalid_item_for_operation_create_item_attachment,

    //! This error is returned from a CreateItem operation if the request
    //! contains an unsupported item type. Supported items include the
    //! following objects:
    //!
    //! \li Item
    //! \li Message
    //! \li CalendarItem
    //! \li Task
    //! \li Contact
    //!
    //! Certain types are created as a side effect of doing something else.
    //! Meeting messages, for example, are created when you send a calendar
    //! item to attendees; they are not explicitly created.
    error_invalid_item_for_operation_create_item,

    //! This error occurs when an attempt is made to use a DeclineItem
    //! response object for an item type other than a meeting request or a
    //! calendar item, or when an attempt is made to decline a calendar item
    //! occurrence that is in the Deleted Items folder.
    error_invalid_item_for_operation_decline_item,

    //! This error indicates that the ExpandDL operation is valid only for
    //! private distribution lists.
    error_invalid_item_for_operation_expand_dl,

    //! This error is returned from a RemoveItem response object if the
    //! request specifies an item that is not a meeting cancellation item.
    error_invalid_item_for_operation_remove_item,

    //! This error is returned from a SendItem operation if the request
    //! specifies an item that is not a message item.
    error_invalid_item_for_operation_send_item,

    //! This error occurs when an attempt is made to use
    //! TentativelyAcceptItem for an item type other than a meeting request
    //! or a calendar item, or when an attempt is made to tentatively accept
    //! a calendar item occurrence that is in the Deleted Items folder.
    error_invalid_item_for_operation_tentative,

    //! This error is for internal use only. This error is not returned.
    error_invalid_logon_type,

    //! This error indicates that the CreateItem operation or the UpdateItem
    //! operation failed while creating or updating a personal distribution
    //! list.
    error_invalid_mailbox,

    //! This error occurs when the structure of the managed folder is
    //! corrupted and cannot be rendered.
    error_invalid_managed_folder_property,

    //! This error occurs when the quota that is set on the managed folder
    //! is less than zero, which indicates a corrupted managed folder.
    error_invalid_managed_folder_quota,

    //! This error occurs when the size that is set on the managed folder is
    //! less than zero, which indicates a corrupted managed folder.
    error_invalid_managed_folder_size,

    //! This error occurs when the supplied merged free/busy internal value
    //! is invalid. The default minimum value is 5 minutes. The default
    //! maximum value is 1440 minutes.
    error_invalid_merged_free_busy_interval,

    //! This error occurs when the name is invalid for the ResolveNames
    //! operation. For example, a zero length string, a single space, a
    //! comma, and a dash are all invalid names.
    error_invalid_name_for_name_resolution,

    //! This error indicates an error in the Network Service account on the
    //! Client Access server.
    error_invalid_network_service_context,

    //! This response code is not used.
    error_invalid_oof_parameter,

    //! This is a general error that is used when the requested operation is
    //! invalid. For example, you cannot do the following:
    //!
    //! \li Perform a "Deep" traversal by using the FindFolder operation on
    //! a public folder.
    //! \li Move or copy the public folder root.
    //! \li Delete an associated item by using any mode except "Hard"
    //! delete.
    //! \li Move or copy an associated item.
    error_invalid_operation,

    //! This error indicates that a caller requested free/busy information
    //! for a user in another organization but the organizational
    //! relationship does not have free/busy enabled.
    error_invalid_organization_relationship_for_free_busy,

    //! This error occurs when a consumer passes in a zero or a negative
    //! value for the maximum rows to be returned.
    error_invalid_paging_max_rows,

    //! This error occurs when a consumer passes in an invalid parent folder
    //! for an operation. For example, this error is returned if you try to
    //! create a folder within a search folder.
    error_invalid_parent_folder,

    //! This error is returned when an attempt is made to set a task
    //! completion percentage to an invalid value. The value must be between
    //! 0 and 100 (inclusive).
    error_invalid_percent_complete_value,

    //! This error indicates that the permission level is inconsistent with
    //! the permission settings.
    error_invalid_permission_settings,

    //! This error indicates that the caller identifier is not valid.
    error_invalid_phone_call_id,

    //! This error indicates that the telephone number is not correct or
    //! does not fit the dial plan rules.
    error_invalid_phone_number,

    //! This error occurs when the property that you are trying to append to
    //! does not support appending. The following are the only properties
    //! that support appending:
    //!
    //! \li Recipient collections (ToRecipients, CcRecipients,
    //! BccRecipients)
    //! \li Attendee collections (RequiredAttendees, OptionalAttendees,
    //! Resources)
    //! \li Body
    //! \li ReplyTo
    //!
    //! In addition, this error occurs when a message body is appended if
    //! the format specified in the request does not match the format of the
    //! item in the store.
    error_invalid_property_append,

    //! This error occurs if the delete operation is specified in an
    //! UpdateItem operation or UpdateFolder operation call for a property
    //! that does not support the delete operation. For example, you cannot
    //! delete the ItemId element of the Item object.
    error_invalid_property_delete,

    //! This error occurs if the consumer passes in one of the flag
    //! properties in an Exists filter. For example, this error occurs if
    //! the IsRead or IsFromMe flags are specified in the Exists element.
    //! The request should use the IsEqualTo element instead for these as
    //! they are flags and therefore part of a single property.
    error_invalid_property_for_exists,

    //! This error occurs when the property that you are trying to
    //! manipulate does not support the operation that is being performed on
    //! it.
    error_invalid_property_for_operation,

    //! This error occurs if a property that is specified in the request is
    //! not available for the item type. For example, this error is returned
    //! if a property that is only available on calendar items is requested
    //! in a GetItem operation call for a message or is updated in an
    //! UpdateItem operation call for a message.
    //!
    //! This occurs in the following operations:
    //!
    //! \li GetItem operation
    //! \li GetFolder operation
    //! \li UpdateItem operation
    //! \li UpdateFolder operation
    error_invalid_property_request,

    //! This error indicates that the property that you are trying to
    //! manipulate does not support the operation that is being performed on
    //! it. For example, this error is returned if the property that you are
    //! trying to set is read-only.
    error_invalid_property_set,

    //! This error occurs during an UpdateItem operation when a client tries
    //! to update certain properties of a message that has already been
    //! sent. For example, the following properties cannot be updated on a
    //! sent message:
    //!
    //! \li IsReadReceiptRequested
    //! \li IsDeliveryReceiptRequested
    error_invalid_property_update_sent_message,

    //! This response code is not used.
    error_invalid_proxy_security_context,

    //! This error occurs if you call the GetEvents operation or the
    //! Unsubscribe operation by using a push subscription ID. To
    //! unsubscribe from a push subscription, you must respond to a push
    //! request with an unsubscribe response, or disconnect your Web service
    //! and wait for the push notifications to time out.
    error_invalid_pull_subscription_id,

    //! This error is returned by the Subscribe operation when it creates a
    //! "push" subscription and indicates that the URL that is included in
    //! the request is invalid. The following conditions must be met for
    //! Exchange Web Services to accept the URL:
    //!
    //! \li String length &gt; 0 and &lt; 2083.
    //! \li Protocol is http or https.
    //! \li The URL can be parsed by the URI Microsoft .NET Framework class.
    error_invalid_push_subscription_url,

    //! This error indicates that the recipient collection on your message
    //! or the attendee collection on your calendar item is invalid. For
    //! example, this error will be returned when an attempt is made to send
    //! an item that has no recipients.
    error_invalid_recipients,

    //! This error indicates that the search folder has a recipient table
    //! filter that Exchange Web Services cannot represent. To get around
    //! this error, retrieve the folder without requesting the search
    //! parameters.
    error_invalid_recipient_subfilter,

    //! This error indicates that the search folder has a recipient table
    //! filter that Exchange Web Services cannot represent. To get around
    //! this error, retrieve the folder without requesting the search
    //! parameters.
    error_invalid_recipient_subfilter_comparison,

    //! This error indicates that the search folder has a recipient table
    //! filter that Exchange Web Services cannot represent. To get around
    //! this error, retrieve the folder without requesting the search
    //! parameters.
    error_invalid_recipient_subfilter_order,

    //! This error indicates that the search folder has a recipient table
    //! filter that Exchange Web Services cannot represent. To get around
    //! this error, retrieve the folder without requesting the search
    //! parameters.
    error_invalid_recipient_subfilter_text_filter,

    //! This error is returned from the CreateItem operation for Forward and
    //! Reply response objects in the following scenarios:
    //!
    //! \li The referenced item identifier is not a Message, a CalendarItem,
    //! or a descendant of a Message or CalendarItem.
    //! \li The reference item identifier is for a CalendarItem and the
    //! organizer is trying to Reply or ReplyAll to himself.
    //! \li The message is a draft and Reply or ReplyAll is selected.
    //! \li The reference item, for SuppressReadReceipt, is not a Message or
    //! a descendant of a Message.
    error_invalid_reference_item,

    //! This error occurs when the SOAP request has a SOAP action header,
    //! but nothing in the SOAP body. Note that the SOAP Action header is
    //! not required as Exchange Web Services can determine the method to
    //! call from the local name of the root element in the SOAP body.
    error_invalid_request,

    //! This response code is not used.
    error_invalid_restriction,

    //! This error is returned when the specified retention tag has an
    //! incorrect action associated with it. This error was introduced in
    //! Exchange 2013.
    error_invalid_retention_tag_type_mismatch,

    //! This error is returned when an attempt is made to set a nonexistent
    //! or invisible tag on a PolicyTag property. This error was introduced
    //! in Exchange 2013.
    error_invalid_retention_tag_invisible,

    //! This error is returned when an attempt is made to set an implicit
    //! tag on the PolicyTag property. This error was introduced in Exchange
    //! 2013.
    error_invalid_retention_tag_inheritance,

    //! Indicates that the retention tag GUID was invalid.
    error_invalid_retention_tag_id_guid,

    //! This error occurs if the routing type that is passed for a
    //! RoutingType (EmailAddressType) element is invalid. Typically, the
    //! routing type is set to Simple Mail Transfer Protocol (SMTP).
    error_invalid_routing_type,

    //! This error occurs if the specified duration end time is not greater
    //! than the start time, or if the end time does not occur in the
    //! future.
    error_invalid_scheduled_oof_duration,

    //! This error indicates that a proxy request that was sent to another
    //! server is not able to service the request due to a versioning
    //! mismatch.
    error_invalid_schema_version_for_mailbox_version,

    //! This error indicates that the Exchange security descriptor on the
    //! Calendar folder in the store is corrupted.
    error_invalid_security_descriptor,

    //! This error occurs during an attempt to send an item where the
    //! SavedItemFolderId is specified in the request but the
    //! **SaveItemToFolder** property is set to false.
    error_invalid_send_item_save_settings,

    //! This error indicates that the token that was passed in the header is
    //! malformed, does not refer to a valid account in the directory, or is
    //! missing the primary group ConnectingSID.
    error_invalid_serialized_access_token,

    //! This error indicates that the sharing metadata is not valid. This
    //! can be caused by invalid XML.
    error_invalid_sharing_data,

    //! This error indicates that the sharing message is not valid. This can
    //! be caused by a missing property.
    error_invalid_sharing_message,

    //! This error occurs when an invalid SID is passed in a request.
    error_invalid_sid,

    //! This error indicates that the SIP name, dial plan, or the phone
    //! number are invalid SIP URIs.
    error_invalid_sip_uri,

    //! This error indicates that an invalid request server version was
    //! specified in the request.
    error_invalid_server_version,

    //! This error occurs when the SMTP address cannot be parsed.
    error_invalid_smtp_address,

    //! This response code is not used.
    error_invalid_subfilter_type,

    //! This response code is not used.
    error_invalid_subfilter_type_not_attendee_type,

    //! This response code is not used.
    error_invalid_subfilter_type_not_recipient_type,

    //! This error indicates that the subscription is no longer valid. This
    //! could be because the Client Access server is restarting or because
    //! the subscription is expired.
    error_invalid_subscription,

    //! This error indicates that the subscribe request included multiple
    //! public folder IDs. A subscription can include multiple folders from
    //! the same mailbox or one public folder ID.
    error_invalid_subscription_request,

    //! This error is returned by SyncFolderItems or SyncFolderHierarchy if
    //! the SyncState data that is passed is invalid. To fix this error, you
    //! must resynchronize without the sync state. Make sure that if you are
    //! persisting sync state BLOBs, you are not accidentally truncating the
    //! BLOB.
    error_invalid_sync_state_data,

    //! This error indicates that the specified time interval is invalid.
    //! The start time must be greater than or equal to the end time.
    error_invalid_time_interval,

    //! This error indicates that an internally inconsistent UserId was
    //! specified for a permissions operation. For example, if a
    //! distinguished user ID is specified (Default or Anonymous), this
    //! error is returned if you also try to specify a SID, or primary SMTP
    //! address or display name for this user.
    error_invalid_user_info,

    //! This error indicates that the user Out of Office (OOF) settings are
    //! invalid because of a missing internal or external reply.
    error_invalid_user_oof_settings,

    //! This error occurs during Exchange Impersonation. The caller passed
    //! in an invalid UPN in the SOAP header that was not accessible in the
    //! directory.
    error_invalid_user_principal_name,

    //! This error occurs when an invalid SID is passed in a request.
    error_invalid_user_sid,

    //! This response code is not used.
    error_invalid_user_sid_missing_upn,

    //! This error indicates that the comparison value in the restriction is
    //! invalid for the property you are comparing against. For example, the
    //! comparison value of DateTimeCreated &gt; true would return this
    //! response code. This response code is also returned if you specify an
    //! enumeration property in the comparison, but the value that you are
    //! comparing against is not a valid value for that enumeration.
    error_invalid_value_for_property,

    //! This error is caused by an invalid watermark.
    error_invalid_watermark,

    //! This error indicates that a valid VoIP gateway is not available.
    error_ip_gateway_not_found,

    //! This error indicates that conflict resolution was unable to resolve
    //! changes for the properties. The items in the store may have been
    //! changed and have to be updated. Retrieve the updated change key and
    //! try again.
    error_irresolvable_conflict,

    //! This error indicates that the state of the object is corrupted and
    //! cannot be retrieved. When you are retrieving an item, only certain
    //! elements will be in this state, such as Body and MimeContent. Omit
    //! these elements and retry the operation.
    error_item_corrupt,

    //! This error occurs when the item was not found or you do not have
    //! permission to access the item.
    error_item_not_found,

    //! This error occurs if a property request on an item fails. The
    //! property may exist, but it could not be retrieved.
    error_item_property_request_failed,

    //! This error occurs when attempts to save the item or folder fail.
    error_item_save,

    //! This error occurs when attempts to save the item or folder fail
    //! because of invalid property values. The response code includes the
    //! path of the invalid properties.
    error_item_save_property_error,

    //! This response code is not used.
    error_legacy_mailbox_free_busy_view_type_not_merged,

    //! This response code is not used.
    error_local_server_object_not_found,

    //! This error indicates that the Availability service was unable to log
    //! on as the network service to proxy requests to the appropriate sites
    //! or forests. This response typically indicates a configuration error.
    error_logon_as_network_service_failed,

    //! This error indicates that the mailbox information in AD DS is
    //! configured incorrectly.
    error_mailbox_configuration,

    //! This error indicates that the MailboxDataArray element in the
    //! request is empty. You must supply at least one mailbox identifier.
    error_mailbox_data_array_empty,

    //! This error occurs when more than 100 entries are supplied in a
    //! MailboxDataArray element..
    error_mailbox_data_array_too_big,

    //! This error indicates that an attempt to access a mailbox failed
    //! because the mailbox is in a failover process.
    error_mailbox_failover,

    //! Indicates that the mailbox hold was not found.
    error_mailbox_hold_not_found,

    //! This error occurs when the connection to the mailbox to get the
    //! calendar view information failed.
    error_mailbox_logon_failed,

    //! This error indicates that the mailbox is being moved to a different
    //! mailbox store or server. This error can also indicate that the
    //! mailbox is on another server or mailbox database.
    error_mailbox_move_in_progress,

    //! This error indicates that one of the following error conditions
    //! occurred:
    //!
    //! \li The mailbox store is corrupt.
    //! \li The mailbox store is being stopped.
    //! \li The mailbox store is offline.
    //! \li A network error occurred when an attempt was made to access the
    //! mailbox store.
    //! \li The mailbox store is overloaded and cannot accept any more
    //! connections.
    //! \li The mailbox store has been paused.
    error_mailbox_store_unavailable,

    //! This error occurs if the MailboxData element information cannot be
    //! mapped to a valid mailbox account.
    error_mail_recipient_not_found,

    //! This error indicates that mail tips are disabled.
    error_mail_tips_disabled,

    //! This error occurs if the managed folder that you are trying to
    //! create already exists in a mailbox.
    error_managed_folder_already_exists,

    //! This error occurs when the folder name that was specified in the
    //! request does not map to a managed folder definition in AD DS. You
    //! can only create instances of managed folders for folders that are
    //! defined in AD DS. Check the name and try again.
    error_managed_folder_not_found,

    //! This error indicates that the managed folders root was deleted from
    //! the mailbox or that a folder exists in the same parent folder that
    //! has the name of the managed folder root. This will also occur if the
    //! attempt to create the root managed folder fails.
    error_managed_folders_root_failure,

    //! This error indicates that the suggestions engine encountered a
    //! problem when it was trying to generate the suggestions.
    error_meeting_suggestion_generation_failed,

    //! This error occurs if the **MessageDisposition** attribute is not
    //! set. This attribute is required for the following:
    //!
    //! \li The CreateItem operation and the UpdateItem operation when the
    //! item being created or updated is a Message.
    //! \li CancelCalendarItem, AcceptItem, DeclineItem, or
    //! TentativelyAcceptItem response objects.
    error_message_disposition_required,

    //! This error indicates that the message that you are trying to send
    //! exceeds the allowed limits.
    error_message_size_exceeded,

    //! This error indicates that the given domain cannot be found.
    error_message_tracking_no_such_domain,

    //! This error indicates that the message tracking service cannot track
    //! the message.
    error_message_tracking_permanent_error,

    //! This error indicates that the message tracking service is either
    //! down or busy. This error code indicates a transient error. Clients
    //! can retry to connect to the server when this error is received.
    _error_message_tracking_transient_error,

    //! This error occurs when the MIME content is not a valid iCal for a
    //! CreateItem operation. For a GetItem operation, this response
    //! indicates that the MIME content could not be generated.
    error_mime_content_conversion_failed,

    //! This error occurs when the MIME content is invalid.
    error_mime_content_invalid,

    //! This error occurs when the MIME content in the request is not a
    //! valid base 64 string.
    error_mime_content_invalid_base64_string,

    //! This error indicates that a required argument was missing from the
    //! request. The response message text indicates which argument to
    //! check.
    error_missing_argument,

    //! This error indicates that you specified a distinguished folder ID in
    //! the request, but the account that made the request does not have a
    //! mailbox on the system. In that case, you must supply a Mailbox sub-
    //! element under DistinguishedFolderId.
    error_missing_email_address,

    //! This error indicates that you specified a distinguished folder ID in
    //! the request, but the account that made the request does not have a
    //! mailbox on the system. In that case, you must supply a Mailbox sub-
    //! element under DistinguishedFolderId. This response is returned from
    //! the CreateManagedFolder operation.
    error_missing_email_address_for_managed_folder,

    //! This error occurs if the EmailAddress (NonEmptyStringType) element
    //! is missing.
    error_missing_information_email_address,

    //! This error occurs if the ReferenceItemId is missing.
    error_missing_information_reference_item_id,

    //! This error is returned when an attempt is made to not include the
    //! item element in the **ItemAttachment** element of a CreateAttachment
    //! operation request.
    error_missing_item_for_create_item_attachment,

    //! This error occurs when the policy IDs property, property tag 0x6732,
    //! for the folder is missing. You should consider this a corrupted
    //! folder.
    error_missing_managed_folder_id,

    //! This error indicates that you tried to send an item without
    //! including recipients. Note that if you call the CreateItem operation
    //! with a message disposition that causes the message to be sent, you
    //! will get the following response code: ErrorInvalidRecipients.
    error_missing_recipients,

    //! This error indicates that a UserId has not been fully specified in a
    //! permissions set.
    error_missing_user_id_information,

    //! This error indicates that you have specified more than one
    //! ExchangeImpersonation element value within a request.
    error_more_than_one_access_mode_specified,

    //! This error indicates that the move or copy operation failed. Moving
    //! occurs in the CreateItem operation when you accept a meeting request
    //! that is in the Deleted Items folder. In addition, if you decline a
    //! meeting request, cancel a calendar item, or remove a meeting from
    //! your calendar, it is moved to the Deleted Items folder.
    error_move_copy_failed,

    //! This error occurs if you try to move a distinguished folder.
    error_move_distinguished_folder,

    //! This error occurs when a request attempts to access multiple mailbox
    //! servers. This error was introduced in Exchange 2013.
    error_multi_legacy_mailbox_access,

    //! This error occurs if the ResolveNames operation returns more than
    //! one result or the ambiguous name that you specified matches more
    //! than one object in the directory. The response code includes the
    //! matched names in the response data.
    error_name_resolution_multiple_results,

    //! This error indicates that the caller does not have a mailbox on the
    //! system. The ResolveNames operation or ExpandDL operation is invalid
    //! for connecting a user without a mailbox.
    error_name_resolution_no_mailbox,

    //! This error indicates that the ResolveNames operation returns no
    //! results.
    error_name_resolution_no_results,

    //! This error code MUST be returned when the Web service cannot find a
    //! server to handle the request.
    error_no_applicable_proxy_cas_servers_available,

    //! This error occurs if there is no Calendar folder for the mailbox.
    error_no_calendar,

    //! This error indicates that the request referred to a mailbox in
    //! another Active Directory site, but no Client Access servers in the
    //! destination site were configured for Windows Authentication, and
    //! therefore the request could not be proxied.
    error_no_destination_cas_due_to_kerberos_requirements,

    //! This error indicates that the request referred to a mailbox in
    //! another Active Directory site, but no Client Access servers in the
    //! destination site were configured for SSL connections, and therefore
    //! the request could not be proxied.
    error_no_destination_cas_due_to_ssl_requirements,

    //! This error indicates that the request referred to a mailbox in
    //! another Active Directory site, but no Client Access servers in the
    //! destination site were of an acceptable product version to receive
    //! the request, and therefore the request could not be proxied.
    error_no_destination_cas_due_to_version_mismatch,

    //! This error occurs if you set the FolderClass element when you are
    //! creating an item other than a generic folder. For typed folders such
    //! as CalendarFolder and TasksFolder, the folder class is implied.
    //! Setting the folder class to a different folder type by using the
    //! UpdateFolder operation results in the ErrorObjectTypeChanged
    //! response. Instead, use a generic folder type but set the folder
    //! class to the value that you require. Exchange Web Services will
    //! create the correct strongly typed folder.
    error_no_folder_class_override,

    //! This error indicates that the caller does not have free/busy viewing
    //! rights on the Calendar folder in question.
    error_no_free_busy_access,

    //! This error occurs in the following scenarios:
    //!
    //! \li The e-mail address is empty in CreateManagedFolder.
    //! \li The e-mail address does not refer to a valid account in a
    //! request that takes an e-mail address in the body or in the SOAP
    //! header, such as in an Exchange Impersonation call.
    error_non_existent_mailbox,

    //! This error occurs when a caller passes in a non-primary SMTP
    //! address. The response includes the correct SMTP address to use.
    error_non_primary_smtp_address,

    //! This error indicates that MAPI properties in the custom range,
    //! 0x8000 and greater, cannot be referenced by property tags. You must
    //! use the EWS Managed API PropertySetId property or the EWS
    //! ExtendedFieldURI element with the PropertySetId attribute.
    error_no_property_tag_for_custom_properties,

    //! This response code is not used.
    error_no_public_folder_replica_available,

    //! This error code MUST be returned if no public folder server is
    //! available or if the caller does not have a home public server.
    error_no_public_folder_server_available,

    //! This error indicates that the request referred to a mailbox in
    //! another Active Directory site, but none of the Client Access servers
    //! in that site responded, and therefore the request could not be
    //! proxied.
    error_no_responding_cas_in_destination_site,

    //! This error indicates that the caller tried to grant permissions in
    //! its calendar or contacts folder to a user in another organization,
    //! and the attempt failed.
    error_not_allowed_external_sharing_by_policy,

    //! This error indicates that the user is not a delegate for the
    //! mailbox. It is returned by the GetDelegate operation, the
    //! RemoveDelegate operation, and the UpdateDelegate operation when the
    //! specified delegate user is not found in the list of delegates.
    error_not_delegate,

    //! This error indicates that the operation could not be completed
    //! because of insufficient memory.
    error_not_enough_memory,

    //! This error indicates that the sharing message is not supported.
    error_not_supported_sharing_message,

    //! This error occurs if the object type changed.
    error_object_type_changed,

    //! This error occurs when the Start or End time of an occurrence is
    //! updated so that the occurrence is scheduled to happen earlier or
    //! later than the corresponding previous or next occurrence.
    error_occurrence_crossing_boundary,

    //! This error indicates that the time allotment for a given occurrence
    //! overlaps with another occurrence of the same recurring item. This
    //! response also occurs when the length in minutes of a given
    //! occurrence is larger than Int32.MaxValue.
    error_occurrence_time_span_too_big,

    //! This error indicates that the current operation is not valid for the
    //! public folder root.
    error_operation_not_allowed_with_public_folder_root,

    //! This error indicates that the requester's organization is not
    //! federated so the requester cannot create sharing messages to send to
    //! an external user or cannot accept sharing messages received from an
    //! external user.
    error_organization_not_federated,

    //! This response code is not used.
    error_parent_folder_id_required,

    //! This error occurs in the CreateFolder operation when the parent
    //! folder is not found.
    error_parent_folder_not_found,

    //! This error indicates that you must change your password before you
    //! can access this mailbox. This occurs when a new account has been
    //! created and the administrator indicated that the user must change
    //! the password at first logon. You cannot update the password by using
    //! Exchange Web Services. You must use a tool such as Microsoft Office
    //! Outlook Web App to change your password.
    error_password_change_required,

    //! This error indicates that the password has expired. You cannot
    //! change the password by using Exchange Web Services. You must use a
    //! tool such as Outlook Web App to change your password.
    error_password_expired,

    //! This error indicates that the requester tried to grant permissions
    //! in its calendar or contacts folder to an external user but the
    //! sharing policy assigned to the requester indicates that the
    //! requested permission level is higher than what the sharing policy
    //! allows.
    error_permission_not_allowed_by_policy,

    //! This error indicates that the telephone number was not in the
    //! correct form.
    error_phone_number_not_dialable,

    //! This error indicates that the update failed because of invalid
    //! property values. The response message includes the invalid property
    //! paths.
    error_property_update,

    //! This error is intended for internal use only. This error was
    //! introduced in Exchange 2013.
    error_prompt_publishing_operation_failed,

    //! This response code is not used.
    error_property_validation_failure,

    //! This error indicates that the request referred to a subscription
    //! that exists on another Client Access server, but an attempt to proxy
    //! the request to that Client Access server failed.
    error_proxied_subscription_call_failure,

    //! This response code is not used.
    error_proxy_call_failed,

    //! This error indicates that the request referred to a mailbox in
    //! another Active Directory site, and the original caller is a member
    //! of more than 3,000 groups.
    error_proxy_group_sid_limit_exceeded,

    //! This error indicates that the request that Exchange Web Services
    //! sent to another Client Access server when trying to fulfill a
    //! GetUserAvailabilityRequest request was invalid. This response code
    //! typically indicates that a configuration or rights error has
    //! occurred, or that someone tried unsuccessfully to mimic an
    //! availability proxy request.
    error_proxy_request_not_allowed,

    //! This error indicates that Exchange Web Services tried to proxy an
    //! availability request to another Client Access server for
    //! fulfillment, but the request failed. This response can be caused by
    //! network connectivity issues or request timeout issues.
    error_proxy_request_processing_failed,

    //! This error code must be returned if the Web service cannot determine
    //! whether the request is to run on the target server or will be
    //! proxied to another server.
    error_proxy_service_discovery_failed,

    //! This response code is not used.
    error_proxy_token_expired,

    //! This error occurs when the public folder mailbox URL cannot be
    //! found. This error is intended for internal use only. This error was
    //! introduced in Exchange 2013.
    error_public_folder_mailbox_discovery_failed,

    //! This error occurs when an attempt is made to access a public folder
    //! and the attempt is unsuccessful. This error was introduced in
    //! Exchange 2013Exchange Server 2013.
    error_public_folder_operation_failed,

    //! This error occurs when the recipient that was passed to the
    //! GetUserAvailability operation is located on a computer that is
    //! running a version of Exchange Server that is earlier than Exchange
    //! 2007, and the request to retrieve free/busy information for the
    //! recipient from the public folder server failed.
    error_public_folder_request_processing_failed,

    //! This error occurs when the recipient that was passed to the
    //! GetUserAvailability operation is located on a computer that is
    //! running a version of Exchange Server that is earlier than Exchange
    //! 2007, and the request to retrieve free/busy information for the
    //! recipient from the public folder server failed because the
    //! organizational unit did not have an associated public folder server.
    error_public_folder_server_not_found,

    //! This error occurs when a synchronization operation succeeds against
    //! the primary public folder mailbox but does not succeed against the
    //! secondary public folder mailbox. This error was introduced in
    //! Exchange 2013.
    error_public_folder_sync_exception,

    //! This error indicates that the search folder restriction may be
    //! valid, but it is not supported by EWS. Exchange Web Services limits
    //! restrictions to contain a maximum of 255 filter expressions. If you
    //! try to bind to an existing search folder that exceeds 255, this
    //! response code is returned.
    error_query_filter_too_long,

    //! This error occurs when the mailbox quota is exceeded.
    error_quota_exceeded,

    //! This error is returned by the GetEvents operation or push
    //! notifications when a failure occurs while retrieving event
    //! information. When this error is returned, the subscription is
    //! deleted. Re-create the event synchronization based on a last known
    //! watermark.
    error_read_events_failed,

    //! This error is returned by the CreateItem operation if an attempt was
    //! made to suppress a read receipt when the message sender did not
    //! request a read receipt on the message or if the message is in the
    //! Junk E-mail folder.
    error_read_receipt_not_pending,

    //! This error occurs when the end date for the recurrence is after
    //! 9/1/4500.
    error_recurrence_end_date_too_big,

    //! This error occurs when the specified recurrence does not have any
    //! occurrence instances in the specified range.
    error_recurrence_has_no_occurrence,

    //! This error indicates that the delegate list failed to be saved after
    //! delegates were removed.
    error_remove_delegates_failed,

    //! This response code is not used.
    error_request_aborted,

    //! This error occurs when the request stream is larger than 400 KB.
    error_request_stream_too_big,

    //! This error is returned when a required property is missing in a
    //! CreateAttachment operation request. The missing property URI is
    //! included in the response.
    error_required_property_missing,

    //! This error indicates that the caller has specified a folder that is
    //! not a contacts folder to the ResolveNames operation.
    error_resolve_names_invalid_folder_type,

    //! This error indicates that the caller has specified more than one
    //! contacts folder to the ResolveNames operation.
    error_resolve_names_only_one_contacts_folder_allowed,

    //! This response code is not used.
    error_response_schema_validation,

    //! This error occurs if the restriction contains more than 255 nodes.
    error_restriction_too_long,

    //! This error occurs when the restriction cannot be evaluated by
    //! Exchange Web Services.
    error_restriction_too_complex,

    //! This error indicates that the number of calendar entries for a given
    //! recipient exceeds the allowed limit of 1000. Reduce the window and
    //! try again.
    error_result_set_too_big,

    //! This error occurs when the SavedItemFolderId is not found.
    error_saved_item_folder_not_found,

    //! This error occurs when the request cannot be validated against the
    //! schema.
    error_schema_validation,

    //! This error indicates that the search folder was created, but the
    //! search criteria were never set on the folder. This only occurs when
    //! you access corrupted search folders that were created by using
    //! another API or client. To fix this error, use the UpdateFolder
    //! operation to set the SearchParameters element to include the
    //! restriction that should be on the folder.
    error_search_folder_not_initialized,

    //! This error occurs when both of the following conditions occur:
    //!
    //! \li A user has been granted CanActAsOwner permissions but is not
    //! granted delegate rights on the principal’s mailbox.
    //! \li The same user tries to create and send an e-mail message in the
    //! principal’s mailbox by using the SendAndSaveCopy option.
    //! The result is an ErrorSendAsDenied error and the creation of the
    //! e-mail message in the principal’s Drafts folder.
    error_send_as_denied,

    //! This error is returned by the DeleteItem operation if the
    //! **SendMeetingCancellations** attribute is missing from the request
    //! and the item to delete is a calendar item.
    error_send_meeting_cancellations_required,

    //! This error is returned by the UpdateItem operation if the
    //! **SendMeetingInvitationsOrCancellations** attribute is missing from
    //! the request and the item to update is a calendar item.
    error_send_meeting_invitations_or_cancellations_required,

    //! This error is returned by the CreateItem operation if the
    //! **SendMeetingInvitations** attribute is missing from the request and
    //! the item to create is a calendar item.
    error_send_meeting_invitations_required,

    //! This error indicates that after the organizer sends a meeting
    //! request, the request cannot be updated. To modify the meeting,
    //! modify the calendar item, not the meeting request.
    error_sent_meeting_request_update,

    //! This error indicates that after the task initiator sends a task
    //! request, that request cannot be updated.
    error_sent_task_request_update,

    //! This error occurs when the server is busy.
    error_server_busy,

    //! This error indicates that Exchange Web Services tried to proxy a
    //! user availability request to the appropriate forest for the
    //! recipient, but it could not determine where to send the request
    //! because of a service discovery failure.
    error_service_discovery_failed,

    //! This error indicates that the external URL property has not been set
    //! in the Active Directory database.
    error_sharing_no_external_ews_available,

    //! This error occurs in an UpdateItem operation or a SendItem operation
    //! when the change key is not up-to-date or was not supplied. Call the
    //! GetItem operation to retrieve an updated change key and then try the
    //! operation again.
    error_stale_object,

    //! This error Indicates that a user cannot immediately send more
    //! requests because the submission quota has been reached.
    error_submission_quota_exceeded,

    //! This error occurs when you try to access a subscription by using an
    //! account that did not create that subscription. Each subscription can
    //! only be accessed by the creator of the subscription.
    error_subscription_access_denied,

    //! This error indicates that you cannot create a subscription if you
    //! are not the owner or do not have owner access to the mailbox.
    error_subscription_delegate_access_not_supported,

    //! This error occurs if the subscription that corresponds to the
    //! specified SubscriptionId (GetEvents) is not found. The subscription
    //! may have expired, the Exchange Web Services process may have been
    //! restarted, or an invalid subscription was passed in. If the
    //! subscription was valid, re-create the subscription with the latest
    //! watermark. This is returned by the Unsubscribe operation or the
    //! GetEvents operation responses.
    error_subscription_not_found,

    //! This error code must be returned when a request is made for a
    //! subscription that has been unsubscribed.
    error_subscription_unsubscribed,

    //! This error is returned by the SyncFolderItems operation if the
    //! parent folder that is specified cannot be found.
    error_sync_folder_not_found,

    //! This error indicates that a team mailbox was not found. This error
    //! was introduced in Exchange 2013.
    error_team_mailbox_not_found,

    //! This error indicates that a team mailbox was found but that it is
    //! not linked to a SharePoint Server. This error was introduced in
    //! Exchange 2013.
    error_team_mailbox_not_linked_to_share_point,

    //! This error indicates that a team mailbox was found but that the link
    //! to the SharePoint Server is not valid. This error was introduced in
    //! Exchange 2013.
    error_team_mailbox_url_validation_failed,

    //! This error code is not used. This error was introduced in Exchange
    //! 2013.
    error_team_mailbox_not_authorized_owner,

    //! This error code is not used. This error was introduced in Exchange
    //! 2013.
    error_team_mailbox_active_to_pending_delete,

    //! This error indicates that an attempt to send a notification to the
    //! team mailbox owners was unsuccessful. This error was introduced in
    //! Exchange 2013.
    error_team_mailbox_failed_sending_notifications,

    //! This error indicates a general error that can occur when trying to
    //! access a team mailbox. Try submitting the request at a later time.
    //! This error was introduced in Exchange 2013.
    error_team_mailbox_error_unknown,

    //! This error indicates that the time window that was specified is
    //! larger than the allowed limit. By default, the allowed limit is 42.
    error_time_interval_too_big,

    //! This error occurs when there is not enough time to complete the
    //! processing of the request.
    error_timeout_expired,

    //! This error indicates that there is a time zone error.
    error_time_zone,

    //! This error indicates that the destination folder does not exist.
    error_to_folder_not_found,

    //! This error occurs if the caller tries to do a Token serialization
    //! request but does not have the ms-Exch-EPI-TokenSerialization right
    //! on the Client Access server.
    error_token_serialization_denied,

    //! This error occurs when the internal limit on open objects has been
    //! exceeded.
    error_too_many_objects_opened,

    //! This error indicates that a user's dial plan is not available.
    error_unified_messaging_dial_plan_not_found,

    //! This error is intended for internal use only. This error was
    //! introduced in Exchange 2013.
    error_unified_messaging_report_data_not_found,

    //! This error is intended for internal use only. This error was
    //! introduced in Exchange 2013.
    error_unified_messaging_prompt_not_found,

    //! This error indicates that the user could not be found.
    error_unified_messaging_request_failed,

    //! This error indicates that a valid server for the dial plan can be
    //! found to handle the request.
    error_unified_messaging_server_not_found,

    //! This response code is not used.
    error_unable_to_get_user_oof_settings,

    //! This error occurs when an unsuccessful attempt is made to remove an
    //! IM contact from a group. This error was introduced in Exchange 2013.
    error_unable_to_remove_im_contact_from_group,

    //! This error occurs when you try to set the **Culture** property to a
    //! value that is not parsable by the
    //! **System.Globalization.CultureInfo** class.
    error_unsupported_culture,

    //! This error occurs when a caller tries to use extended properties of
    //! types object, object array, error, or null.
    error_unsupported_mapi_property_type,

    //! This error occurs when you are trying to retrieve or set MIME
    //! content for an item other than a PostItem, Message, or CalendarItem
    //! object.
    error_unsupported_mime_conversion,

    //! This error occurs when the caller passes a property that is invalid
    //! for a query. This can occur when calculated properties are used.
    error_unsupported_path_for_query,

    //! This error occurs when the caller passes a property that is invalid
    //! for a sort or group by property. This can occur when calculated
    //! properties are used.
    error_unsupported_path_for_sort_group,

    //! This response code is not used.
    error_unsupported_property_definition,

    //! This error indicates that the search folder restriction may be
    //! valid, but it is not supported by EWS.
    error_unsupported_query_filter,

    //! This error indicates that the specified recurrence is not supported
    //! for tasks.
    error_unsupported_recurrence,

    //! This response code is not used.
    error_unsupported_sub_filter,

    //! This error indicates that Exchange Web Services found a property
    //! type in the store but it cannot generate XML for the property type.
    error_unsupported_type_for_conversion,

    //! This error indicates that the delegate list failed to be saved after
    //! delegates were updated.
    error_update_delegates_failed,

    //! This error occurs when the single property path that is listed in a
    //! change description does not match the single property that is being
    //! set within the actual Item or Folder object.
    error_update_property_mismatch,

    //! This error indicates that the requester is not enabled.
    error_user_not_unified_messaging_enabled,

    //! This error indicates that the requester tried to grant permissions
    //! in its calendar or contacts folder to an external user but the
    //! sharing policy assigned to the requester indicates that the domain
    //! of the external user is not listed in the policy.
    error_user_not_allowed_by_policy,

    //! Indicates that the requester's organization has a set of federated
    //! domains but the requester's organization does not have any SMTP
    //! proxy addresses with one of the federated domains.
    error_user_without_federated_proxy_address,

    //! This error indicates that a calendar view start date or end date was
    //! set to 1/1/0001 12:00:00 AM or 12/31/9999 11:59:59 PM.
    error_value_out_of_range,

    //! This error indicates that the Exchange store detected a virus in the
    //! message.
    error_virus_detected,

    //! This error indicates that the Exchange store detected a virus in the
    //! message and deleted it.
    error_virus_message_deleted,

    //! This response code is not used.
    error_voice_mail_not_implemented,

    //! This response code is not used.
    error_web_request_in_invalid_state,

    //! This error indicates that there was an internal failure during
    //! communication with unmanaged code.
    error_win32_interop_error,

    //! This response code is not used.
    error_working_hours_save_failed,

    //! This response code is not used.
    error_working_hours_xml_malformed,

    //! This error indicates that a request can only be made to a server
    //! that is the same version as the mailbox server.
    error_wrong_server_version,

    //! This error indicates that a request was made by a delegate that has
    //! a different server version than the principal's mailbox server.
    error_wrong_server_version_delegate,

    //! This error code is never returned.
    error_missing_information_sharing_folder_id,

    //! Specifies that there are duplicate SOAP headers.
    error_duplicate_soap_header,

    //! Specifies that an attempt at synchronizing a sharing folder failed.
    //! This error code is returned when:
    //!
    //! \li The subscription for a sharing folder is not found.
    //! \li The sharing folder was not found.
    //! \li The corresponding directory user was not found.
    //! \li The user no longer exists.
    //! \li The appointment is invalid.
    //! \li The contact item is invalid.
    //! \li There was a communication failure with the remote server.
    error_sharing_synchronization_failed,

    //! Specifies that either the message tracking service is down or busy.
    //! This error code specifies a transient error. Clients can retry to
    //! connect to the server when this error is received.
    error_message_tracking_transient_error,

    //! This error MUST be returned if an action cannot be applied to one or
    //! more items in the conversation.
    error_apply_conversation_action_failed,

    //! This error MUST be returned if any rule does not validate.
    error_inbox_rules_validation_error,

    //! This error MUST be returned when an attempt to manage Inbox rules
    //! occurs after another client has accessed the Inbox rules.
    error_outlook_rule_blob_exists,

    //! This error MUST be returned when a user's rule quota has been
    //! exceeded.
    error_rules_over_quota,

    //! This error MUST be returned to the first subscription connection if
    //! a second subscription connection is opened.
    error_new_event_stream_connection_opened,

    //! This error MUST be returned when event notifications are missed.
    error_missed_notification_events,

    //! This error is returned when there are duplicate legacy distinguished
    //! names in Active Directory Domain Services (AD DS). This error was
    //! introduced in Exchange 2013.
    error_duplicate_legacy_distinguished_name,

    //! This error indicates that a request to get a client access token was
    //! not valid. This error was introduced in Exchange 2013.
    error_invalid_client_access_token_request,

    //! This error is intended for internal use only. This error was
    //! introduced in Exchange 2013.
    error_no_speech_detected,

    //! This error is intended for internal use only. This error was
    //! introduced in Exchange 2013.
    error_um_server_unavailable,

    //! This error is intended for internal use only. This error was
    //! introduced in Exchange 2013.
    error_recipient_not_found,

    //! This error is intended for internal use only. This error was
    //! introduced in Exchange 2013.
    error_recognizer_not_installed,

    //! This error is intended for internal use only. This error was
    //! introduced in Exchange 2013.
    error_speech_grammar_error,

    //! This error is returned if the ManagementRole header in the SOAP
    //! header is incorrect. This error was introduced in Exchange 2013.
    error_invalid_management_role_header,

    //! This error is intended for internal use only. This error was
    //! introduced in Exchange 2013.
    error_location_services_disabled,

    //! This error is intended for internal use only. This error was
    //! introduced in Exchange 2013.
    error_location_services_request_timed_out,

    //! This error is intended for internal use only. This error was
    //! introduced in Exchange 2013.
    error_location_services_request_failed,

    //! This error is intended for internal use only. This error was
    //! introduced in Exchange 2013.
    error_location_services_invalid_request,

    //! This error is intended for internal use only.
    error_weather_service_disabled,

    //! This error is returned when a scoped search attempt is performed
    //! without using a QueryString (String) element for a content indexing
    //! search. This is applicable to the SearchMailboxes and
    //! FindConversation operations. This error was introduced in Exchange
    //! 2013.
    error_mailbox_scope_not_allowed_without_query_string,

    //! This error is returned when an archive mailbox search is
    //! unsuccessful. This error was introduced in Exchange 2013.
    error_archive_mailbox_search_failed,

    //! This error is returned when the URL of an archive mailbox is not
    //! discoverable. This error was introduced in Exchange 2013.
    error_archive_mailbox_service_discovery_failed,

    //! This error occurs when the operation to get the remote archive
    //! mailbox folder failed.
    error_get_remote_archive_folder_failed,

    //! This error occurs when the operation to find the remote archive
    //! mailbox folder failed.
    error_find_remote_archive_folder_failed,

    //! This error occurs when the operation to get the remote archive
    //! mailbox item failed.
    error_get_remote_archive_item_failed,

    //! This error occurs when the operation to export remote archive
    //! mailbox items failed.
    error_export_remote_archive_items_failed,

    //! This error is returned if an invalid photo size is requested from
    //! the server. This error was introduced in Exchange 2013.
    error_invalid_photo_size,

    //! This error is returned when an unexpected photo size is requested in
    //! a GetUserPhoto operation request. This error was introduced in
    //! Exchange 2013.
    error_search_query_has_too_many_keywords,

    //! This error is returned when a SearchMailboxes operation request
    //! contains too many mailboxes to search. This error was introduced in
    //! Exchange 2013.
    error_search_too_many_mailboxes,

    //! This error indicates that no retention tags were found for this
    //! user. This error was introduced in Exchange 2013.
    error_invalid_retention_tag_none,

    //! This error is returned when discovery searches are disabled on a
    //! tenant or server. This error was introduced in Exchange 2013.
    error_discovery_searches_disabled,

    //! This error occurs when attempting to invoke the FindItem operation
    //! with a SeekToConditionPageItemView for fetching calendar items,
    //! which is not supported.
    error_calendar_seek_to_condition_not_supported,

    //! This error is intended for internal use only.
    error_calendar_is_group_mailbox_for_accept,

    //! This error is intended for internal use only.
    error_calendar_is_group_mailbox_for_decline,

    //! This error is intended for internal use only.
    error_calendar_is_group_mailbox_for_tentative,

    //! This error is intended for internal use only.
    error_calendar_is_group_mailbox_for_suppress_read_receipt,

    //! The tenant is marked for removal.
    error_organization_access_blocked,

    //! The user doesn't have a valid license.
    error_invalid_license,

    //! The message per folder receive quota has been exceeded.
    error_message_per_folder_count_receive_quota_exceeded
};

namespace internal
{
    inline response_code str_to_response_code(const std::string& str)
    {
        if (str == "NoError")
        {
            return response_code::no_error;
        }
        if (str == "ErrorAccessDenied")
        {
            return response_code::error_access_denied;
        }
        if (str == "ErrorAccessModeSpecified")
        {
            return response_code::error_access_mode_specified;
        }
        if (str == "ErrorAccountDisabled")
        {
            return response_code::error_account_disabled;
        }
        if (str == "ErrorAddDelegatesFailed")
        {
            return response_code::error_add_delegates_failed;
        }
        if (str == "ErrorAddressSpaceNotFound")
        {
            return response_code::error_address_space_not_found;
        }
        if (str == "ErrorADOperation")
        {
            return response_code::error_ad_operation;
        }
        if (str == "ErrorADSessionFilter")
        {
            return response_code::error_ad_session_filter;
        }
        if (str == "ErrorADUnavailable")
        {
            return response_code::error_ad_unavailable;
        }
        if (str == "ErrorAffectedTaskOccurrencesRequired")
        {
            return response_code::error_affected_task_occurrences_required;
        }
        if (str == "ErrorArchiveFolderPathCreation")
        {
            return response_code::error_archive_folder_path_creation;
        }
        if (str == "ErrorArchiveMailboxNotEnabled")
        {
            return response_code::error_archive_mailbox_not_enabled;
        }
        if (str == "ErrorArchiveMailboxServiceDiscoveryFailed")
        {
            return response_code::
                error_archive_mailbox_service_discovery_failed;
        }
        if (str == "ErrorAttachmentNestLevelLimitExceeded")
        {
            return response_code::error_attachment_nest_level_limit_exceeded;
        }
        if (str == "ErrorAttachmentSizeLimitExceeded")
        {
            return response_code::error_attachment_size_limit_exceeded;
        }
        if (str == "ErrorAutoDiscoverFailed")
        {
            return response_code::error_auto_discover_failed;
        }
        if (str == "ErrorAvailabilityConfigNotFound")
        {
            return response_code::error_availability_config_not_found;
        }
        if (str == "ErrorBatchProcessingStopped")
        {
            return response_code::error_batch_processing_stopped;
        }
        if (str == "ErrorCalendarCannotMoveOrCopyOccurrence")
        {
            return response_code::error_calendar_cannot_move_or_copy_occurrence;
        }
        if (str == "ErrorCalendarCannotUpdateDeletedItem")
        {
            return response_code::error_calendar_cannot_update_deleted_item;
        }
        if (str == "ErrorCalendarCannotUseIdForOccurrenceId")
        {
            return response_code::
                error_calendar_cannot_use_id_for_occurrence_id;
        }
        if (str == "ErrorCalendarCannotUseIdForRecurringMasterId")
        {
            return response_code::
                error_calendar_cannot_use_id_for_recurring_master_id;
        }
        if (str == "ErrorCalendarDurationIsTooLong")
        {
            return response_code::error_calendar_duration_is_too_long;
        }
        if (str == "ErrorCalendarEndDateIsEarlierThanStartDate")
        {
            return response_code::
                error_calendar_end_date_is_earlier_than_start_date;
        }
        if (str == "ErrorCalendarFolderIsInvalidForCalendarView")
        {
            return response_code::
                error_calendar_folder_is_invalid_for_calendar_view;
        }
        if (str == "ErrorCalendarInvalidAttributeValue")
        {
            return response_code::error_calendar_invalid_attribute_value;
        }
        if (str == "ErrorCalendarInvalidDayForTimeChangePattern")
        {
            return response_code::
                error_calendar_invalid_day_for_time_change_pattern;
        }
        if (str == "ErrorCalendarInvalidDayForWeeklyRecurrence")
        {
            return response_code::
                error_calendar_invalid_day_for_weekly_recurrence;
        }
        if (str == "ErrorCalendarInvalidPropertyState")
        {
            return response_code::error_calendar_invalid_property_state;
        }
        if (str == "ErrorCalendarInvalidPropertyValue")
        {
            return response_code::error_calendar_invalid_property_value;
        }
        if (str == "ErrorCalendarInvalidRecurrence")
        {
            return response_code::error_calendar_invalid_recurrence;
        }
        if (str == "ErrorCalendarInvalidTimeZone")
        {
            return response_code::error_calendar_invalid_time_zone;
        }
        if (str == "ErrorCalendarIsCancelledForAccept")
        {
            return response_code::error_calendar_is_cancelled_for_accept;
        }
        if (str == "ErrorCalendarIsCancelledForDecline")
        {
            return response_code::error_calendar_is_cancelled_for_decline;
        }
        if (str == "ErrorCalendarIsCancelledForRemove")
        {
            return response_code::error_calendar_is_cancelled_for_remove;
        }
        if (str == "ErrorCalendarIsCancelledForTentative")
        {
            return response_code::error_calendar_is_cancelled_for_tentative;
        }
        if (str == "ErrorCalendarIsDelegatedForAccept")
        {
            return response_code::error_calendar_is_delegated_for_accept;
        }
        if (str == "ErrorCalendarIsDelegatedForDecline")
        {
            return response_code::error_calendar_is_delegated_for_decline;
        }
        if (str == "ErrorCalendarIsDelegatedForRemove")
        {
            return response_code::error_calendar_is_delegated_for_remove;
        }
        if (str == "ErrorCalendarIsDelegatedForTentative")
        {
            return response_code::error_calendar_is_delegated_for_tentative;
        }
        if (str == "ErrorCalendarIsNotOrganizer")
        {
            return response_code::error_calendar_is_not_organizer;
        }
        if (str == "ErrorCalendarIsOrganizerForAccept")
        {
            return response_code::error_calendar_is_organizer_for_accept;
        }
        if (str == "ErrorCalendarIsOrganizerForDecline")
        {
            return response_code::error_calendar_is_organizer_for_decline;
        }
        if (str == "ErrorCalendarIsOrganizerForRemove")
        {
            return response_code::error_calendar_is_organizer_for_remove;
        }
        if (str == "ErrorCalendarIsOrganizerForTentative")
        {
            return response_code::error_calendar_is_organizer_for_tentative;
        }
        if (str == "ErrorCalendarMeetingRequestIsOutOfDate")
        {
            return response_code::error_calendar_meeting_request_is_out_of_date;
        }
        if (str == "ErrorCalendarOccurrenceIndexIsOutOfRecurrenceRange")
        {
            return response_code::
                error_calendar_occurrence_index_is_out_of_recurrence_range;
        }
        if (str == "ErrorCalendarOccurrenceIsDeletedFromRecurrence")
        {
            return response_code::
                error_calendar_occurrence_is_deleted_from_recurrence;
        }
        if (str == "ErrorCalendarOutOfRange")
        {
            return response_code::error_calendar_out_of_range;
        }
        if (str == "ErrorCalendarViewRangeTooBig")
        {
            return response_code::error_calendar_view_range_too_big;
        }
        if (str == "ErrorCallerIsInvalidADAccount")
        {
            return response_code::error_caller_is_invalid_ad_account;
        }
        if (str == "ErrorCannotArchiveCalendarContactTaskFolderException")
        {
            return response_code::
                error_cannot_archive_calendar_contact_task_folder_exception;
        }
        if (str == "ErrorCannotArchiveItemsInPublicFolders")
        {
            return response_code::error_cannot_archive_items_in_public_folders;
        }
        if (str == "ErrorCannotArchiveItemsInArchiveMailbox")
        {
            return response_code::error_cannot_archive_items_in_archive_mailbox;
        }
        if (str == "ErrorCannotCreateCalendarItemInNonCalendarFolder")
        {
            return response_code::
                error_cannot_create_calendar_item_in_non_calendar_folder;
        }
        if (str == "ErrorCannotCreateContactInNonContactFolder")
        {
            return response_code::
                error_cannot_create_contact_in_non_contact_folder;
        }
        if (str == "ErrorCannotCreatePostItemInNonMailFolder")
        {
            return response_code::
                error_cannot_create_post_item_in_non_mail_folder;
        }
        if (str == "ErrorCannotCreateTaskInNonTaskFolder")
        {
            return response_code::error_cannot_create_task_in_non_task_folder;
        }
        if (str == "ErrorCannotDeleteObject")
        {
            return response_code::error_cannot_delete_object;
        }
        if (str == "ErrorCannotDeleteTaskOccurrence")
        {
            return response_code::error_cannot_delete_task_occurrence;
        }
        if (str == "ErrorCannotDisableMandatoryExtension")
        {
            return response_code::error_cannot_disable_mandatory_extension;
        }
        if (str == "ErrorCannotEmptyFolder")
        {
            return response_code::error_cannot_empty_folder;
        }
        if (str == "ErrorCannotGetSourceFolderPath")
        {
            return response_code::error_cannot_get_source_folder_path;
        }
        if (str == "ErrorCannotGetExternalEcpUrl")
        {
            return response_code::error_cannot_get_external_ecp_url;
        }
        if (str == "ErrorCannotOpenFileAttachment")
        {
            return response_code::error_cannot_open_file_attachment;
        }
        if (str == "ErrorCannotSetCalendarPermissionOnNonCalendarFolder")
        {
            return response_code::
                error_cannot_set_calendar_permission_on_non_calendar_folder;
        }
        if (str == "ErrorCannotSetNonCalendarPermissionOnCalendarFolder")
        {
            return response_code::
                error_cannot_set_non_calendar_permission_on_calendar_folder;
        }
        if (str == "ErrorCannotSetPermissionUnknownEntries")
        {
            return response_code::error_cannot_set_permission_unknown_entries;
        }
        if (str == "ErrorCannotSpecifySearchFolderAsSourceFolder")
        {
            return response_code::
                error_cannot_specify_search_folder_as_source_folder;
        }
        if (str == "ErrorCannotUseFolderIdForItemId")
        {
            return response_code::error_cannot_use_folder_id_for_item_id;
        }
        if (str == "ErrorCannotUseItemIdForFolderId")
        {
            return response_code::error_cannot_use_item_id_for_folder_id;
        }
        if (str == "ErrorChangeKeyRequired")
        {
            return response_code::error_change_key_required;
        }
        if (str == "ErrorChangeKeyRequiredForWriteOperations")
        {
            return response_code::
                error_change_key_required_for_write_operations;
        }
        if (str == "ErrorClientDisconnected")
        {
            return response_code::error_client_disconnected;
        }
        if (str == "ErrorClientIntentInvalidStateDefinition")
        {
            return response_code::error_client_intent_invalid_state_definition;
        }
        if (str == "ErrorClientIntentNotFound")
        {
            return response_code::error_client_intent_not_found;
        }
        if (str == "ErrorConnectionFailed")
        {
            return response_code::error_connection_failed;
        }
        if (str == "ErrorContainsFilterWrongType")
        {
            return response_code::error_contains_filter_wrong_type;
        }
        if (str == "ErrorContentConversionFailed")
        {
            return response_code::error_content_conversion_failed;
        }
        if (str == "ErrorContentIndexingNotEnabled")
        {
            return response_code::error_content_indexing_not_enabled;
        }
        if (str == "ErrorCorruptData")
        {
            return response_code::error_corrupt_data;
        }
        if (str == "ErrorCreateItemAccessDenied")
        {
            return response_code::error_create_item_access_denied;
        }
        if (str == "ErrorCreateManagedFolderPartialCompletion")
        {
            return response_code::
                error_create_managed_folder_partial_completion;
        }
        if (str == "ErrorCreateSubfolderAccessDenied")
        {
            return response_code::error_create_subfolder_access_denied;
        }
        if (str == "ErrorCrossMailboxMoveCopy")
        {
            return response_code::error_cross_mailbox_move_copy;
        }
        if (str == "ErrorCrossSiteRequest")
        {
            return response_code::error_cross_site_request;
        }
        if (str == "ErrorDataSizeLimitExceeded")
        {
            return response_code::error_data_size_limit_exceeded;
        }
        if (str == "ErrorDataSourceOperation")
        {
            return response_code::error_data_source_operation;
        }
        if (str == "ErrorDelegateAlreadyExists")
        {
            return response_code::error_delegate_already_exists;
        }
        if (str == "ErrorDelegateCannotAddOwner")
        {
            return response_code::error_delegate_cannot_add_owner;
        }
        if (str == "ErrorDelegateMissingConfiguration")
        {
            return response_code::error_delegate_missing_configuration;
        }
        if (str == "ErrorDelegateNoUser")
        {
            return response_code::error_delegate_no_user;
        }
        if (str == "ErrorDelegateValidationFailed")
        {
            return response_code::error_delegate_validation_failed;
        }
        if (str == "ErrorDeleteDistinguishedFolder")
        {
            return response_code::error_delete_distinguished_folder;
        }
        if (str == "ErrorDeleteItemsFailed")
        {
            return response_code::error_delete_items_failed;
        }
        if (str == "ErrorDeleteUnifiedMessagingPromptFailed")
        {
            return response_code::error_delete_unified_messaging_prompt_failed;
        }
        if (str == "ErrorDistinguishedUserNotSupported")
        {
            return response_code::error_distinguished_user_not_supported;
        }
        if (str == "ErrorDistributionListMemberNotExist")
        {
            return response_code::error_distribution_list_member_not_exist;
        }
        if (str == "ErrorDuplicateInputFolderNames")
        {
            return response_code::error_duplicate_input_folder_names;
        }
        if (str == "ErrorDuplicateUserIdsSpecified")
        {
            return response_code::error_duplicate_user_ids_specified;
        }
        if (str == "ErrorEmailAddressMismatch")
        {
            return response_code::error_email_address_mismatch;
        }
        if (str == "ErrorEventNotFound")
        {
            return response_code::error_event_not_found;
        }
        if (str == "ErrorExceededConnectionCount")
        {
            return response_code::error_exceeded_connection_count;
        }
        if (str == "ErrorExceededSubscriptionCount")
        {
            return response_code::error_exceeded_subscription_count;
        }
        if (str == "ErrorExceededFindCountLimit")
        {
            return response_code::error_exceeded_find_count_limit;
        }
        if (str == "ErrorExpiredSubscription")
        {
            return response_code::error_expired_subscription;
        }
        if (str == "ErrorExtensionNotFound")
        {
            return response_code::error_extension_not_found;
        }
        if (str == "ErrorFolderCorrupt")
        {
            return response_code::error_folder_corrupt;
        }
        if (str == "ErrorFolderExists")
        {
            return response_code::error_folder_exists;
        }
        if (str == "ErrorFolderNotFound")
        {
            return response_code::error_folder_not_found;
        }
        if (str == "ErrorFolderPropertyRequestFailed")
        {
            return response_code::error_folder_property_request_failed;
        }
        if (str == "ErrorFolderSave")
        {
            return response_code::error_folder_save;
        }
        if (str == "ErrorFolderSaveFailed")
        {
            return response_code::error_folder_save_failed;
        }
        if (str == "ErrorFolderSavePropertyError")
        {
            return response_code::error_folder_save_property_error;
        }
        if (str == "ErrorFreeBusyGenerationFailed")
        {
            return response_code::error_free_busy_generation_failed;
        }
        if (str == "ErrorGetServerSecurityDescriptorFailed")
        {
            return response_code::error_get_server_security_descriptor_failed;
        }
        if (str == "ErrorImContactLimitReached")
        {
            return response_code::error_im_contact_limit_reached;
        }
        if (str == "ErrorImGroupDisplayNameAlreadyExists")
        {
            return response_code::error_im_group_display_name_already_exists;
        }
        if (str == "ErrorImGroupLimitReached")
        {
            return response_code::error_im_group_limit_reached;
        }
        if (str == "ErrorImpersonateUserDenied")
        {
            return response_code::error_impersonate_user_denied;
        }
        if (str == "ErrorImpersonationDenied")
        {
            return response_code::error_impersonation_denied;
        }
        if (str == "ErrorImpersonationFailed")
        {
            return response_code::error_impersonation_failed;
        }
        if (str == "ErrorIncorrectSchemaVersion")
        {
            return response_code::error_incorrect_schema_version;
        }
        if (str == "ErrorIncorrectUpdatePropertyCount")
        {
            return response_code::error_incorrect_update_property_count;
        }
        if (str == "ErrorIndividualMailboxLimitReached")
        {
            return response_code::error_individual_mailbox_limit_reached;
        }
        if (str == "ErrorInsufficientResources")
        {
            return response_code::error_insufficient_resources;
        }
        if (str == "ErrorInternalServerError")
        {
            return response_code::error_internal_server_error;
        }
        if (str == "ErrorInternalServerTransientError")
        {
            return response_code::error_internal_server_transient_error;
        }
        if (str == "ErrorInvalidAccessLevel")
        {
            return response_code::error_invalid_access_level;
        }
        if (str == "ErrorInvalidArgument")
        {
            return response_code::error_invalid_argument;
        }
        if (str == "ErrorInvalidAttachmentId")
        {
            return response_code::error_invalid_attachment_id;
        }
        if (str == "ErrorInvalidAttachmentSubfilter")
        {
            return response_code::error_invalid_attachment_subfilter;
        }
        if (str == "ErrorInvalidAttachmentSubfilterTextFilter")
        {
            return response_code::
                error_invalid_attachment_subfilter_text_filter;
        }
        if (str == "ErrorInvalidAuthorizationContext")
        {
            return response_code::error_invalid_authorization_context;
        }
        if (str == "ErrorInvalidChangeKey")
        {
            return response_code::error_invalid_change_key;
        }
        if (str == "ErrorInvalidClientSecurityContext")
        {
            return response_code::error_invalid_client_security_context;
        }
        if (str == "ErrorInvalidCompleteDate")
        {
            return response_code::error_invalid_complete_date;
        }
        if (str == "ErrorInvalidContactEmailAddress")
        {
            return response_code::error_invalid_contact_email_address;
        }
        if (str == "ErrorInvalidContactEmailIndex")
        {
            return response_code::error_invalid_contact_email_index;
        }
        if (str == "ErrorInvalidCrossForestCredentials")
        {
            return response_code::error_invalid_cross_forest_credentials;
        }
        if (str == "ErrorInvalidDelegatePermission")
        {
            return response_code::error_invalid_delegate_permission;
        }
        if (str == "ErrorInvalidDelegateUserId")
        {
            return response_code::error_invalid_delegate_user_id;
        }
        if (str == "ErrorInvalidExchangeImpersonationHeaderData")
        {
            return response_code::
                error_invalid_exchange_impersonation_header_data;
        }
        if (str == "ErrorInvalidExcludesRestriction")
        {
            return response_code::error_invalid_excludes_restriction;
        }
        if (str == "ErrorInvalidExpressionTypeForSubFilter")
        {
            return response_code::error_invalid_expression_type_for_sub_filter;
        }
        if (str == "ErrorInvalidExtendedProperty")
        {
            return response_code::error_invalid_extended_property;
        }
        if (str == "ErrorInvalidExtendedPropertyValue")
        {
            return response_code::error_invalid_extended_property_value;
        }
        if (str == "ErrorInvalidExternalSharingInitiator")
        {
            return response_code::error_invalid_external_sharing_initiator;
        }
        if (str == "ErrorInvalidExternalSharingSubscriber")
        {
            return response_code::error_invalid_external_sharing_subscriber;
        }
        if (str == "ErrorInvalidFederatedOrganizationId")
        {
            return response_code::error_invalid_federated_organization_id;
        }
        if (str == "ErrorInvalidFolderId")
        {
            return response_code::error_invalid_folder_id;
        }
        if (str == "ErrorInvalidFolderTypeForOperation")
        {
            return response_code::error_invalid_folder_type_for_operation;
        }
        if (str == "ErrorInvalidFractionalPagingParameters")
        {
            return response_code::error_invalid_fractional_paging_parameters;
        }
        if (str == "ErrorInvalidGetSharingFolderRequest")
        {
            return response_code::error_invalid_get_sharing_folder_request;
        }
        if (str == "ErrorInvalidFreeBusyViewType")
        {
            return response_code::error_invalid_free_busy_view_type;
        }
        if (str == "ErrorInvalidId")
        {
            return response_code::error_invalid_id;
        }
        if (str == "ErrorInvalidIdEmpty")
        {
            return response_code::error_invalid_id_empty;
        }
        if (str == "ErrorInvalidLikeRequest")
        {
            return response_code::error_invalid_like_request;
        }
        if (str == "ErrorInvalidIdMalformed")
        {
            return response_code::error_invalid_id_malformed;
        }
        if (str == "ErrorInvalidIdMalformedEwsLegacyIdFormat")
        {
            return response_code::
                error_invalid_id_malformed_ews_legacy_id_format;
        }
        if (str == "ErrorInvalidIdMonikerTooLong")
        {
            return response_code::error_invalid_id_moniker_too_long;
        }
        if (str == "ErrorInvalidIdNotAnItemAttachmentId")
        {
            return response_code::error_invalid_id_not_an_item_attachment_id;
        }
        if (str == "ErrorInvalidIdReturnedByResolveNames")
        {
            return response_code::error_invalid_id_returned_by_resolve_names;
        }
        if (str == "ErrorInvalidIdStoreObjectIdTooLong")
        {
            return response_code::error_invalid_id_store_object_id_too_long;
        }
        if (str == "ErrorInvalidIdTooManyAttachmentLevels")
        {
            return response_code::error_invalid_id_too_many_attachment_levels;
        }
        if (str == "ErrorInvalidIdXml")
        {
            return response_code::error_invalid_id_xml;
        }
        if (str == "ErrorInvalidImContactId")
        {
            return response_code::error_invalid_im_contact_id;
        }
        if (str == "ErrorInvalidImDistributionGroupSmtpAddress")
        {
            return response_code::
                error_invalid_im_distribution_group_smtp_address;
        }
        if (str == "ErrorInvalidImGroupId")
        {
            return response_code::error_invalid_im_group_id;
        }
        if (str == "ErrorInvalidIndexedPagingParameters")
        {
            return response_code::error_invalid_indexed_paging_parameters;
        }
        if (str == "ErrorInvalidInternetHeaderChildNodes")
        {
            return response_code::error_invalid_internet_header_child_nodes;
        }
        if (str == "ErrorInvalidItemForOperationArchiveItem")
        {
            return response_code::error_invalid_item_for_operation_archive_item;
        }
        if (str == "ErrorInvalidItemForOperationAcceptItem")
        {
            return response_code::error_invalid_item_for_operation_accept_item;
        }
        if (str == "ErrorInvalidItemForOperationCancelItem")
        {
            return response_code::error_invalid_item_for_operation_cancel_item;
        }
        if (str == "ErrorInvalidItemForOperationCreateItemAttachment")
        {
            return response_code::
                error_invalid_item_for_operation_create_item_attachment;
        }
        if (str == "ErrorInvalidItemForOperationCreateItem")
        {
            return response_code::error_invalid_item_for_operation_create_item;
        }
        if (str == "ErrorInvalidItemForOperationDeclineItem")
        {
            return response_code::error_invalid_item_for_operation_decline_item;
        }
        if (str == "ErrorInvalidItemForOperationExpandDL")
        {
            return response_code::error_invalid_item_for_operation_expand_dl;
        }
        if (str == "ErrorInvalidItemForOperationRemoveItem")
        {
            return response_code::error_invalid_item_for_operation_remove_item;
        }
        if (str == "ErrorInvalidItemForOperationSendItem")
        {
            return response_code::error_invalid_item_for_operation_send_item;
        }
        if (str == "ErrorInvalidItemForOperationTentative")
        {
            return response_code::error_invalid_item_for_operation_tentative;
        }
        if (str == "ErrorInvalidLogonType")
        {
            return response_code::error_invalid_logon_type;
        }
        if (str == "ErrorInvalidMailbox")
        {
            return response_code::error_invalid_mailbox;
        }
        if (str == "ErrorInvalidManagedFolderProperty")
        {
            return response_code::error_invalid_managed_folder_property;
        }
        if (str == "ErrorInvalidManagedFolderQuota")
        {
            return response_code::error_invalid_managed_folder_quota;
        }
        if (str == "ErrorInvalidManagedFolderSize")
        {
            return response_code::error_invalid_managed_folder_size;
        }
        if (str == "ErrorInvalidMergedFreeBusyInterval")
        {
            return response_code::error_invalid_merged_free_busy_interval;
        }
        if (str == "ErrorInvalidNameForNameResolution")
        {
            return response_code::error_invalid_name_for_name_resolution;
        }
        if (str == "ErrorInvalidNetworkServiceContext")
        {
            return response_code::error_invalid_network_service_context;
        }
        if (str == "ErrorInvalidOofParameter")
        {
            return response_code::error_invalid_oof_parameter;
        }
        if (str == "ErrorInvalidOperation")
        {
            return response_code::error_invalid_operation;
        }
        if (str == "ErrorInvalidOrganizationRelationshipForFreeBusy")
        {
            return response_code::
                error_invalid_organization_relationship_for_free_busy;
        }
        if (str == "ErrorInvalidPagingMaxRows")
        {
            return response_code::error_invalid_paging_max_rows;
        }
        if (str == "ErrorInvalidParentFolder")
        {
            return response_code::error_invalid_parent_folder;
        }
        if (str == "ErrorInvalidPercentCompleteValue")
        {
            return response_code::error_invalid_percent_complete_value;
        }
        if (str == "ErrorInvalidPermissionSettings")
        {
            return response_code::error_invalid_permission_settings;
        }
        if (str == "ErrorInvalidPhoneCallId")
        {
            return response_code::error_invalid_phone_call_id;
        }
        if (str == "ErrorInvalidPhoneNumber")
        {
            return response_code::error_invalid_phone_number;
        }
        if (str == "ErrorInvalidPropertyAppend")
        {
            return response_code::error_invalid_property_append;
        }
        if (str == "ErrorInvalidPropertyDelete")
        {
            return response_code::error_invalid_property_delete;
        }
        if (str == "ErrorInvalidPropertyForExists")
        {
            return response_code::error_invalid_property_for_exists;
        }
        if (str == "ErrorInvalidPropertyForOperation")
        {
            return response_code::error_invalid_property_for_operation;
        }
        if (str == "ErrorInvalidPropertyRequest")
        {
            return response_code::error_invalid_property_request;
        }
        if (str == "ErrorInvalidPropertySet")
        {
            return response_code::error_invalid_property_set;
        }
        if (str == "ErrorInvalidPropertyUpdateSentMessage")
        {
            return response_code::error_invalid_property_update_sent_message;
        }
        if (str == "ErrorInvalidProxySecurityContext")
        {
            return response_code::error_invalid_proxy_security_context;
        }
        if (str == "ErrorInvalidPullSubscriptionId")
        {
            return response_code::error_invalid_pull_subscription_id;
        }
        if (str == "ErrorInvalidPushSubscriptionUrl")
        {
            return response_code::error_invalid_push_subscription_url;
        }
        if (str == "ErrorInvalidRecipients")
        {
            return response_code::error_invalid_recipients;
        }
        if (str == "ErrorInvalidRecipientSubfilter")
        {
            return response_code::error_invalid_recipient_subfilter;
        }
        if (str == "ErrorInvalidRecipientSubfilterComparison")
        {
            return response_code::error_invalid_recipient_subfilter_comparison;
        }
        if (str == "ErrorInvalidRecipientSubfilterOrder")
        {
            return response_code::error_invalid_recipient_subfilter_order;
        }
        if (str == "ErrorInvalidRecipientSubfilterTextFilter")
        {
            return response_code::error_invalid_recipient_subfilter_text_filter;
        }
        if (str == "ErrorInvalidReferenceItem")
        {
            return response_code::error_invalid_reference_item;
        }
        if (str == "ErrorInvalidRequest")
        {
            return response_code::error_invalid_request;
        }
        if (str == "ErrorInvalidRestriction")
        {
            return response_code::error_invalid_restriction;
        }
        if (str == "ErrorInvalidRetentionTagTypeMismatch")
        {
            return response_code::error_invalid_retention_tag_type_mismatch;
        }
        if (str == "ErrorInvalidRetentionTagInvisible")
        {
            return response_code::error_invalid_retention_tag_invisible;
        }
        if (str == "ErrorInvalidRetentionTagInheritance")
        {
            return response_code::error_invalid_retention_tag_inheritance;
        }
        if (str == "ErrorInvalidRetentionTagIdGuid")
        {
            return response_code::error_invalid_retention_tag_id_guid;
        }
        if (str == "ErrorInvalidRoutingType")
        {
            return response_code::error_invalid_routing_type;
        }
        if (str == "ErrorInvalidScheduledOofDuration")
        {
            return response_code::error_invalid_scheduled_oof_duration;
        }
        if (str == "ErrorInvalidSchemaVersionForMailboxVersion")
        {
            return response_code::
                error_invalid_schema_version_for_mailbox_version;
        }
        if (str == "ErrorInvalidSecurityDescriptor")
        {
            return response_code::error_invalid_security_descriptor;
        }
        if (str == "ErrorInvalidSendItemSaveSettings")
        {
            return response_code::error_invalid_send_item_save_settings;
        }
        if (str == "ErrorInvalidSerializedAccessToken")
        {
            return response_code::error_invalid_serialized_access_token;
        }
        if (str == "ErrorInvalidSharingData")
        {
            return response_code::error_invalid_sharing_data;
        }
        if (str == "ErrorInvalidSharingMessage")
        {
            return response_code::error_invalid_sharing_message;
        }
        if (str == "ErrorInvalidSid")
        {
            return response_code::error_invalid_sid;
        }
        if (str == "ErrorInvalidSIPUri")
        {
            return response_code::error_invalid_sip_uri;
        }
        if (str == "ErrorInvalidServerVersion")
        {
            return response_code::error_invalid_server_version;
        }
        if (str == "ErrorInvalidSmtpAddress")
        {
            return response_code::error_invalid_smtp_address;
        }
        if (str == "ErrorInvalidSubfilterType")
        {
            return response_code::error_invalid_subfilter_type;
        }
        if (str == "ErrorInvalidSubfilterTypeNotAttendeeType")
        {
            return response_code::
                error_invalid_subfilter_type_not_attendee_type;
        }
        if (str == "ErrorInvalidSubfilterTypeNotRecipientType")
        {
            return response_code::
                error_invalid_subfilter_type_not_recipient_type;
        }
        if (str == "ErrorInvalidSubscription")
        {
            return response_code::error_invalid_subscription;
        }
        if (str == "ErrorInvalidSubscriptionRequest")
        {
            return response_code::error_invalid_subscription_request;
        }
        if (str == "ErrorInvalidSyncStateData")
        {
            return response_code::error_invalid_sync_state_data;
        }
        if (str == "ErrorInvalidTimeInterval")
        {
            return response_code::error_invalid_time_interval;
        }
        if (str == "ErrorInvalidUserInfo")
        {
            return response_code::error_invalid_user_info;
        }
        if (str == "ErrorInvalidUserOofSettings")
        {
            return response_code::error_invalid_user_oof_settings;
        }
        if (str == "ErrorInvalidUserPrincipalName")
        {
            return response_code::error_invalid_user_principal_name;
        }
        if (str == "ErrorInvalidUserSid")
        {
            return response_code::error_invalid_user_sid;
        }
        if (str == "ErrorInvalidUserSidMissingUPN")
        {
            return response_code::error_invalid_user_sid_missing_upn;
        }
        if (str == "ErrorInvalidValueForProperty")
        {
            return response_code::error_invalid_value_for_property;
        }
        if (str == "ErrorInvalidWatermark")
        {
            return response_code::error_invalid_watermark;
        }
        if (str == "ErrorIPGatewayNotFound")
        {
            return response_code::error_ip_gateway_not_found;
        }
        if (str == "ErrorIrresolvableConflict")
        {
            return response_code::error_irresolvable_conflict;
        }
        if (str == "ErrorItemCorrupt")
        {
            return response_code::error_item_corrupt;
        }
        if (str == "ErrorItemNotFound")
        {
            return response_code::error_item_not_found;
        }
        if (str == "ErrorItemPropertyRequestFailed")
        {
            return response_code::error_item_property_request_failed;
        }
        if (str == "ErrorItemSave")
        {
            return response_code::error_item_save;
        }
        if (str == "ErrorItemSavePropertyError")
        {
            return response_code::error_item_save_property_error;
        }
        if (str == "ErrorLegacyMailboxFreeBusyViewTypeNotMerged")
        {
            return response_code::
                error_legacy_mailbox_free_busy_view_type_not_merged;
        }
        if (str == "ErrorLocalServerObjectNotFound")
        {
            return response_code::error_local_server_object_not_found;
        }
        if (str == "ErrorLogonAsNetworkServiceFailed")
        {
            return response_code::error_logon_as_network_service_failed;
        }
        if (str == "ErrorMailboxConfiguration")
        {
            return response_code::error_mailbox_configuration;
        }
        if (str == "ErrorMailboxDataArrayEmpty")
        {
            return response_code::error_mailbox_data_array_empty;
        }
        if (str == "ErrorMailboxDataArrayTooBig")
        {
            return response_code::error_mailbox_data_array_too_big;
        }
        if (str == "ErrorMailboxFailover")
        {
            return response_code::error_mailbox_failover;
        }
        if (str == "ErrorMailboxHoldNotFound")
        {
            return response_code::error_mailbox_hold_not_found;
        }
        if (str == "ErrorMailboxLogonFailed")
        {
            return response_code::error_mailbox_logon_failed;
        }
        if (str == "ErrorMailboxMoveInProgress")
        {
            return response_code::error_mailbox_move_in_progress;
        }
        if (str == "ErrorMailboxStoreUnavailable")
        {
            return response_code::error_mailbox_store_unavailable;
        }
        if (str == "ErrorMailRecipientNotFound")
        {
            return response_code::error_mail_recipient_not_found;
        }
        if (str == "ErrorMailTipsDisabled")
        {
            return response_code::error_mail_tips_disabled;
        }
        if (str == "ErrorManagedFolderAlreadyExists")
        {
            return response_code::error_managed_folder_already_exists;
        }
        if (str == "ErrorManagedFolderNotFound")
        {
            return response_code::error_managed_folder_not_found;
        }
        if (str == "ErrorManagedFoldersRootFailure")
        {
            return response_code::error_managed_folders_root_failure;
        }
        if (str == "ErrorMeetingSuggestionGenerationFailed")
        {
            return response_code::error_meeting_suggestion_generation_failed;
        }
        if (str == "ErrorMessageDispositionRequired")
        {
            return response_code::error_message_disposition_required;
        }
        if (str == "ErrorMessageSizeExceeded")
        {
            return response_code::error_message_size_exceeded;
        }
        if (str == "ErrorMessageTrackingNoSuchDomain")
        {
            return response_code::error_message_tracking_no_such_domain;
        }
        if (str == "ErrorMessageTrackingPermanentError")
        {
            return response_code::error_message_tracking_permanent_error;
        }
        if (str == " ErrorMessageTrackingTransientError")
        {
            return response_code::_error_message_tracking_transient_error;
        }
        if (str == "ErrorMimeContentConversionFailed")
        {
            return response_code::error_mime_content_conversion_failed;
        }
        if (str == "ErrorMimeContentInvalid")
        {
            return response_code::error_mime_content_invalid;
        }
        if (str == "ErrorMimeContentInvalidBase64String")
        {
            return response_code::error_mime_content_invalid_base64_string;
        }
        if (str == "ErrorMissingArgument")
        {
            return response_code::error_missing_argument;
        }
        if (str == "ErrorMissingEmailAddress")
        {
            return response_code::error_missing_email_address;
        }
        if (str == "ErrorMissingEmailAddressForManagedFolder")
        {
            return response_code::
                error_missing_email_address_for_managed_folder;
        }
        if (str == "ErrorMissingInformationEmailAddress")
        {
            return response_code::error_missing_information_email_address;
        }
        if (str == "ErrorMissingInformationReferenceItemId")
        {
            return response_code::error_missing_information_reference_item_id;
        }
        if (str == "ErrorMissingItemForCreateItemAttachment")
        {
            return response_code::error_missing_item_for_create_item_attachment;
        }
        if (str == "ErrorMissingManagedFolderId")
        {
            return response_code::error_missing_managed_folder_id;
        }
        if (str == "ErrorMissingRecipients")
        {
            return response_code::error_missing_recipients;
        }
        if (str == "ErrorMissingUserIdInformation")
        {
            return response_code::error_missing_user_id_information;
        }
        if (str == "ErrorMoreThanOneAccessModeSpecified")
        {
            return response_code::error_more_than_one_access_mode_specified;
        }
        if (str == "ErrorMoveCopyFailed")
        {
            return response_code::error_move_copy_failed;
        }
        if (str == "ErrorMoveDistinguishedFolder")
        {
            return response_code::error_move_distinguished_folder;
        }
        if (str == "ErrorMultiLegacyMailboxAccess")
        {
            return response_code::error_multi_legacy_mailbox_access;
        }
        if (str == "ErrorNameResolutionMultipleResults")
        {
            return response_code::error_name_resolution_multiple_results;
        }
        if (str == "ErrorNameResolutionNoMailbox")
        {
            return response_code::error_name_resolution_no_mailbox;
        }
        if (str == "ErrorNameResolutionNoResults")
        {
            return response_code::error_name_resolution_no_results;
        }
        if (str == "ErrorNoApplicableProxyCASServersAvailable")
        {
            return response_code::
                error_no_applicable_proxy_cas_servers_available;
        }
        if (str == "ErrorNoCalendar")
        {
            return response_code::error_no_calendar;
        }
        if (str == "ErrorNoDestinationCASDueToKerberosRequirements")
        {
            return response_code::
                error_no_destination_cas_due_to_kerberos_requirements;
        }
        if (str == "ErrorNoDestinationCASDueToSSLRequirements")
        {
            return response_code::
                error_no_destination_cas_due_to_ssl_requirements;
        }
        if (str == "ErrorNoDestinationCASDueToVersionMismatch")
        {
            return response_code::
                error_no_destination_cas_due_to_version_mismatch;
        }
        if (str == "ErrorNoFolderClassOverride")
        {
            return response_code::error_no_folder_class_override;
        }
        if (str == "ErrorNoFreeBusyAccess")
        {
            return response_code::error_no_free_busy_access;
        }
        if (str == "ErrorNonExistentMailbox")
        {
            return response_code::error_non_existent_mailbox;
        }
        if (str == "ErrorNonPrimarySmtpAddress")
        {
            return response_code::error_non_primary_smtp_address;
        }
        if (str == "ErrorNoPropertyTagForCustomProperties")
        {
            return response_code::error_no_property_tag_for_custom_properties;
        }
        if (str == "ErrorNoPublicFolderReplicaAvailable")
        {
            return response_code::error_no_public_folder_replica_available;
        }
        if (str == "ErrorNoPublicFolderServerAvailable")
        {
            return response_code::error_no_public_folder_server_available;
        }
        if (str == "ErrorNoRespondingCASInDestinationSite")
        {
            return response_code::error_no_responding_cas_in_destination_site;
        }
        if (str == "ErrorNotDelegate")
        {
            return response_code::error_not_delegate;
        }
        if (str == "ErrorNotEnoughMemory")
        {
            return response_code::error_not_enough_memory;
        }
        if (str == "ErrorNotSupportedSharingMessage")
        {
            return response_code::error_not_supported_sharing_message;
        }
        if (str == "ErrorObjectTypeChanged")
        {
            return response_code::error_object_type_changed;
        }
        if (str == "ErrorOccurrenceCrossingBoundary")
        {
            return response_code::error_occurrence_crossing_boundary;
        }
        if (str == "ErrorOccurrenceTimeSpanTooBig")
        {
            return response_code::error_occurrence_time_span_too_big;
        }
        if (str == "ErrorOperationNotAllowedWithPublicFolderRoot")
        {
            return response_code::
                error_operation_not_allowed_with_public_folder_root;
        }
        if (str == "ErrorOrganizationNotFederated")
        {
            return response_code::error_organization_not_federated;
        }
        if (str == "ErrorParentFolderIdRequired")
        {
            return response_code::error_parent_folder_id_required;
        }
        if (str == "ErrorParentFolderNotFound")
        {
            return response_code::error_parent_folder_not_found;
        }
        if (str == "ErrorPasswordChangeRequired")
        {
            return response_code::error_password_change_required;
        }
        if (str == "ErrorPasswordExpired")
        {
            return response_code::error_password_expired;
        }
        if (str == "ErrorPermissionNotAllowedByPolicy")
        {
            return response_code::error_permission_not_allowed_by_policy;
        }
        if (str == "ErrorPhoneNumberNotDialable")
        {
            return response_code::error_phone_number_not_dialable;
        }
        if (str == "ErrorPropertyUpdate")
        {
            return response_code::error_property_update;
        }
        if (str == "ErrorPromptPublishingOperationFailed")
        {
            return response_code::error_prompt_publishing_operation_failed;
        }
        if (str == "ErrorPropertyValidationFailure")
        {
            return response_code::error_property_validation_failure;
        }
        if (str == "ErrorProxiedSubscriptionCallFailure")
        {
            return response_code::error_proxied_subscription_call_failure;
        }
        if (str == "ErrorProxyCallFailed")
        {
            return response_code::error_proxy_call_failed;
        }
        if (str == "ErrorProxyGroupSidLimitExceeded")
        {
            return response_code::error_proxy_group_sid_limit_exceeded;
        }
        if (str == "ErrorProxyRequestNotAllowed")
        {
            return response_code::error_proxy_request_not_allowed;
        }
        if (str == "ErrorProxyRequestProcessingFailed")
        {
            return response_code::error_proxy_request_processing_failed;
        }
        if (str == "ErrorProxyServiceDiscoveryFailed")
        {
            return response_code::error_proxy_service_discovery_failed;
        }
        if (str == "ErrorProxyTokenExpired")
        {
            return response_code::error_proxy_token_expired;
        }
        if (str == "ErrorPublicFolderMailboxDiscoveryFailed")
        {
            return response_code::error_public_folder_mailbox_discovery_failed;
        }
        if (str == "ErrorPublicFolderOperationFailed")
        {
            return response_code::error_public_folder_operation_failed;
        }
        if (str == "ErrorPublicFolderRequestProcessingFailed")
        {
            return response_code::error_public_folder_request_processing_failed;
        }
        if (str == "ErrorPublicFolderServerNotFound")
        {
            return response_code::error_public_folder_server_not_found;
        }
        if (str == "ErrorPublicFolderSyncException")
        {
            return response_code::error_public_folder_sync_exception;
        }
        if (str == "ErrorQueryFilterTooLong")
        {
            return response_code::error_query_filter_too_long;
        }
        if (str == "ErrorQuotaExceeded")
        {
            return response_code::error_quota_exceeded;
        }
        if (str == "ErrorReadEventsFailed")
        {
            return response_code::error_read_events_failed;
        }
        if (str == "ErrorReadReceiptNotPending")
        {
            return response_code::error_read_receipt_not_pending;
        }
        if (str == "ErrorRecurrenceEndDateTooBig")
        {
            return response_code::error_recurrence_end_date_too_big;
        }
        if (str == "ErrorRecurrenceHasNoOccurrence")
        {
            return response_code::error_recurrence_has_no_occurrence;
        }
        if (str == "ErrorRemoveDelegatesFailed")
        {
            return response_code::error_remove_delegates_failed;
        }
        if (str == "ErrorRequestAborted")
        {
            return response_code::error_request_aborted;
        }
        if (str == "ErrorRequestStreamTooBig")
        {
            return response_code::error_request_stream_too_big;
        }
        if (str == "ErrorRequiredPropertyMissing")
        {
            return response_code::error_required_property_missing;
        }
        if (str == "ErrorResolveNamesInvalidFolderType")
        {
            return response_code::error_resolve_names_invalid_folder_type;
        }
        if (str == "ErrorResolveNamesOnlyOneContactsFolderAllowed")
        {
            return response_code::
                error_resolve_names_only_one_contacts_folder_allowed;
        }
        if (str == "ErrorResponseSchemaValidation")
        {
            return response_code::error_response_schema_validation;
        }
        if (str == "ErrorRestrictionTooLong")
        {
            return response_code::error_restriction_too_long;
        }
        if (str == "ErrorRestrictionTooComplex")
        {
            return response_code::error_restriction_too_complex;
        }
        if (str == "ErrorResultSetTooBig")
        {
            return response_code::error_result_set_too_big;
        }
        if (str == "ErrorSavedItemFolderNotFound")
        {
            return response_code::error_saved_item_folder_not_found;
        }
        if (str == "ErrorSchemaValidation")
        {
            return response_code::error_schema_validation;
        }
        if (str == "ErrorSearchFolderNotInitialized")
        {
            return response_code::error_search_folder_not_initialized;
        }
        if (str == "ErrorSendAsDenied")
        {
            return response_code::error_send_as_denied;
        }
        if (str == "ErrorSendMeetingCancellationsRequired")
        {
            return response_code::error_send_meeting_cancellations_required;
        }
        if (str == "ErrorSendMeetingInvitationsOrCancellationsRequired")
        {
            return response_code::
                error_send_meeting_invitations_or_cancellations_required;
        }
        if (str == "ErrorSendMeetingInvitationsRequired")
        {
            return response_code::error_send_meeting_invitations_required;
        }
        if (str == "ErrorSentMeetingRequestUpdate")
        {
            return response_code::error_sent_meeting_request_update;
        }
        if (str == "ErrorSentTaskRequestUpdate")
        {
            return response_code::error_sent_task_request_update;
        }
        if (str == "ErrorServerBusy")
        {
            return response_code::error_server_busy;
        }
        if (str == "ErrorServiceDiscoveryFailed")
        {
            return response_code::error_service_discovery_failed;
        }
        if (str == "ErrorStaleObject")
        {
            return response_code::error_stale_object;
        }
        if (str == "ErrorSubmissionQuotaExceeded")
        {
            return response_code::error_submission_quota_exceeded;
        }
        if (str == "ErrorSubscriptionAccessDenied")
        {
            return response_code::error_subscription_access_denied;
        }
        if (str == "ErrorSubscriptionDelegateAccessNotSupported")
        {
            return response_code::
                error_subscription_delegate_access_not_supported;
        }
        if (str == "ErrorSubscriptionNotFound")
        {
            return response_code::error_subscription_not_found;
        }
        if (str == "ErrorSubscriptionUnsubscribed")
        {
            return response_code::error_subscription_unsubscribed;
        }
        if (str == "ErrorSyncFolderNotFound")
        {
            return response_code::error_sync_folder_not_found;
        }
        if (str == "ErrorTeamMailboxNotFound")
        {
            return response_code::error_team_mailbox_not_found;
        }
        if (str == "ErrorTeamMailboxNotLinkedToSharePoint")
        {
            return response_code::error_team_mailbox_not_linked_to_share_point;
        }
        if (str == "ErrorTeamMailboxUrlValidationFailed")
        {
            return response_code::error_team_mailbox_url_validation_failed;
        }
        if (str == "ErrorTeamMailboxNotAuthorizedOwner")
        {
            return response_code::error_team_mailbox_not_authorized_owner;
        }
        if (str == "ErrorTeamMailboxActiveToPendingDelete")
        {
            return response_code::error_team_mailbox_active_to_pending_delete;
        }
        if (str == "ErrorTeamMailboxFailedSendingNotifications")
        {
            return response_code::
                error_team_mailbox_failed_sending_notifications;
        }
        if (str == "ErrorTeamMailboxErrorUnknown")
        {
            return response_code::error_team_mailbox_error_unknown;
        }
        if (str == "ErrorTimeIntervalTooBig")
        {
            return response_code::error_time_interval_too_big;
        }
        if (str == "ErrorTimeoutExpired")
        {
            return response_code::error_timeout_expired;
        }
        if (str == "ErrorTimeZone")
        {
            return response_code::error_time_zone;
        }
        if (str == "ErrorToFolderNotFound")
        {
            return response_code::error_to_folder_not_found;
        }
        if (str == "ErrorTokenSerializationDenied")
        {
            return response_code::error_token_serialization_denied;
        }
        if (str == "ErrorTooManyObjectsOpened")
        {
            return response_code::error_too_many_objects_opened;
        }
        if (str == "ErrorUnifiedMessagingDialPlanNotFound")
        {
            return response_code::error_unified_messaging_dial_plan_not_found;
        }
        if (str == "ErrorUnifiedMessagingReportDataNotFound")
        {
            return response_code::error_unified_messaging_report_data_not_found;
        }
        if (str == "ErrorUnifiedMessagingPromptNotFound")
        {
            return response_code::error_unified_messaging_prompt_not_found;
        }
        if (str == "ErrorUnifiedMessagingRequestFailed")
        {
            return response_code::error_unified_messaging_request_failed;
        }
        if (str == "ErrorUnifiedMessagingServerNotFound")
        {
            return response_code::error_unified_messaging_server_not_found;
        }
        if (str == "ErrorUnableToGetUserOofSettings")
        {
            return response_code::error_unable_to_get_user_oof_settings;
        }
        if (str == "ErrorUnableToRemoveImContactFromGroup")
        {
            return response_code::error_unable_to_remove_im_contact_from_group;
        }
        if (str == "ErrorUnsupportedCulture")
        {
            return response_code::error_unsupported_culture;
        }
        if (str == "ErrorUnsupportedMapiPropertyType")
        {
            return response_code::error_unsupported_mapi_property_type;
        }
        if (str == "ErrorUnsupportedMimeConversion")
        {
            return response_code::error_unsupported_mime_conversion;
        }
        if (str == "ErrorUnsupportedPathForQuery")
        {
            return response_code::error_unsupported_path_for_query;
        }
        if (str == "ErrorUnsupportedPathForSortGroup")
        {
            return response_code::error_unsupported_path_for_sort_group;
        }
        if (str == "ErrorUnsupportedPropertyDefinition")
        {
            return response_code::error_unsupported_property_definition;
        }
        if (str == "ErrorUnsupportedQueryFilter")
        {
            return response_code::error_unsupported_query_filter;
        }
        if (str == "ErrorUnsupportedRecurrence")
        {
            return response_code::error_unsupported_recurrence;
        }
        if (str == "ErrorUnsupportedSubFilter")
        {
            return response_code::error_unsupported_sub_filter;
        }
        if (str == "ErrorUnsupportedTypeForConversion")
        {
            return response_code::error_unsupported_type_for_conversion;
        }
        if (str == "ErrorUpdateDelegatesFailed")
        {
            return response_code::error_update_delegates_failed;
        }
        if (str == "ErrorUpdatePropertyMismatch")
        {
            return response_code::error_update_property_mismatch;
        }
        if (str == "ErrorUserNotUnifiedMessagingEnabled")
        {
            return response_code::error_user_not_unified_messaging_enabled;
        }
        if (str == "ErrorUserNotAllowedByPolicy")
        {
            return response_code::error_user_not_allowed_by_policy;
        }
        if (str == "ErrorUserWithoutFederatedProxyAddress")
        {
            return response_code::error_user_without_federated_proxy_address;
        }
        if (str == "ErrorValueOutOfRange")
        {
            return response_code::error_value_out_of_range;
        }
        if (str == "ErrorVirusDetected")
        {
            return response_code::error_virus_detected;
        }
        if (str == "ErrorVirusMessageDeleted")
        {
            return response_code::error_virus_message_deleted;
        }
        if (str == "ErrorVoiceMailNotImplemented")
        {
            return response_code::error_voice_mail_not_implemented;
        }
        if (str == "ErrorWebRequestInInvalidState")
        {
            return response_code::error_web_request_in_invalid_state;
        }
        if (str == "ErrorWin32InteropError")
        {
            return response_code::error_win32_interop_error;
        }
        if (str == "ErrorWorkingHoursSaveFailed")
        {
            return response_code::error_working_hours_save_failed;
        }
        if (str == "ErrorWorkingHoursXmlMalformed")
        {
            return response_code::error_working_hours_xml_malformed;
        }
        if (str == "ErrorWrongServerVersion")
        {
            return response_code::error_wrong_server_version;
        }
        if (str == "ErrorWrongServerVersionDelegate")
        {
            return response_code::error_wrong_server_version_delegate;
        }
        if (str == "ErrorMissingInformationSharingFolderId")
        {
            return response_code::error_missing_information_sharing_folder_id;
        }
        if (str == "ErrorDuplicateSOAPHeader")
        {
            return response_code::error_duplicate_soap_header;
        }
        if (str == "ErrorSharingSynchronizationFailed")
        {
            return response_code::error_sharing_synchronization_failed;
        }
        if (str == "ErrorSharingNoExternalEwsAvailable")
        {
            return response_code::error_sharing_no_external_ews_available;
        }
        if (str == "ErrorFreeBusyDLLimitReached")
        {
            return response_code::error_free_busy_dl_limit_reached;
        }
        if (str == "ErrorNotAllowedExternalSharingByPolicy")
        {
            return response_code::error_not_allowed_external_sharing_by_policy;
        }
        if (str == "ErrorMessageTrackingTransientError")
        {
            return response_code::error_message_tracking_transient_error;
        }
        if (str == "ErrorApplyConversationActionFailed")
        {
            return response_code::error_apply_conversation_action_failed;
        }
        if (str == "ErrorInboxRulesValidationError")
        {
            return response_code::error_inbox_rules_validation_error;
        }
        if (str == "ErrorOutlookRuleBlobExists")
        {
            return response_code::error_outlook_rule_blob_exists;
        }
        if (str == "ErrorRulesOverQuota")
        {
            return response_code::error_rules_over_quota;
        }
        if (str == "ErrorNewEventStreamConnectionOpened")
        {
            return response_code::error_new_event_stream_connection_opened;
        }
        if (str == "ErrorMissedNotificationEvents")
        {
            return response_code::error_missed_notification_events;
        }
        if (str == "ErrorDuplicateLegacyDistinguishedName")
        {
            return response_code::error_duplicate_legacy_distinguished_name;
        }
        if (str == "ErrorInvalidClientAccessTokenRequest")
        {
            return response_code::error_invalid_client_access_token_request;
        }
        if (str == "ErrorNoSpeechDetected")
        {
            return response_code::error_no_speech_detected;
        }
        if (str == "ErrorUMServerUnavailable")
        {
            return response_code::error_um_server_unavailable;
        }
        if (str == "ErrorRecipientNotFound")
        {
            return response_code::error_recipient_not_found;
        }
        if (str == "ErrorRecognizerNotInstalled")
        {
            return response_code::error_recognizer_not_installed;
        }
        if (str == "ErrorSpeechGrammarError")
        {
            return response_code::error_speech_grammar_error;
        }
        if (str == "ErrorInvalidManagementRoleHeader")
        {
            return response_code::error_invalid_management_role_header;
        }
        if (str == "ErrorLocationServicesDisabled")
        {
            return response_code::error_location_services_disabled;
        }
        if (str == "ErrorLocationServicesRequestTimedOut")
        {
            return response_code::error_location_services_request_timed_out;
        }
        if (str == "ErrorLocationServicesRequestFailed")
        {
            return response_code::error_location_services_request_failed;
        }
        if (str == "ErrorLocationServicesInvalidRequest")
        {
            return response_code::error_location_services_invalid_request;
        }
        if (str == "ErrorWeatherServiceDisabled")
        {
            return response_code::error_weather_service_disabled;
        }
        if (str == "ErrorMailboxScopeNotAllowedWithoutQueryString")
        {
            return response_code::
                error_mailbox_scope_not_allowed_without_query_string;
        }
        if (str == "ErrorArchiveMailboxSearchFailed")
        {
            return response_code::error_archive_mailbox_search_failed;
        }
        if (str == "ErrorGetRemoteArchiveFolderFailed")
        {
            return response_code::error_get_remote_archive_folder_failed;
        }
        if (str == "ErrorFindRemoteArchiveFolderFailed")
        {
            return response_code::error_find_remote_archive_folder_failed;
        }
        if (str == "ErrorGetRemoteArchiveItemFailed")
        {
            return response_code::error_get_remote_archive_item_failed;
        }
        if (str == "ErrorExportRemoteArchiveItemsFailed")
        {
            return response_code::error_export_remote_archive_items_failed;
        }
        if (str == "ErrorInvalidPhotoSize")
        {
            return response_code::error_invalid_photo_size;
        }
        if (str == "ErrorSearchQueryHasTooManyKeywords")
        {
            return response_code::error_search_query_has_too_many_keywords;
        }
        if (str == "ErrorSearchTooManyMailboxes")
        {
            return response_code::error_search_too_many_mailboxes;
        }
        if (str == "ErrorInvalidRetentionTagNone")
        {
            return response_code::error_invalid_retention_tag_none;
        }
        if (str == "ErrorDiscoverySearchesDisabled")
        {
            return response_code::error_discovery_searches_disabled;
        }
        if (str == "ErrorCalendarSeekToConditionNotSupported")
        {
            return response_code::
                error_calendar_seek_to_condition_not_supported;
        }
        if (str == "ErrorCalendarIsGroupMailboxForAccept")
        {
            return response_code::error_calendar_is_group_mailbox_for_accept;
        }
        if (str == "ErrorCalendarIsGroupMailboxForDecline")
        {
            return response_code::error_calendar_is_group_mailbox_for_decline;
        }
        if (str == "ErrorCalendarIsGroupMailboxForTentative")
        {
            return response_code::error_calendar_is_group_mailbox_for_tentative;
        }
        if (str == "ErrorCalendarIsGroupMailboxForSuppressReadReceipt")
        {
            return response_code::
                error_calendar_is_group_mailbox_for_suppress_read_receipt;
        }
        if (str == "ErrorOrganizationAccessBlocked")
        {
            return response_code::error_organization_access_blocked;
        }
        if (str == "ErrorInvalidLicense")
        {
            return response_code::error_invalid_license;
        }
        if (str == "ErrorMessagePerFolderCountReceiveQuotaExceeded")
        {
            return response_code::
                error_message_per_folder_count_receive_quota_exceeded;
        }
        throw exception("Unrecognized response code: " + str);
    }

    inline std::string enum_to_str(response_code code)
    {
        switch (code)
        {
        case response_code::no_error:
            return "NoError";
        case response_code::error_access_denied:
            return "ErrorAccessDenied";
        case response_code::error_access_mode_specified:
            return "ErrorAccessModeSpecified";
        case response_code::error_account_disabled:
            return "ErrorAccountDisabled";
        case response_code::error_add_delegates_failed:
            return "ErrorAddDelegatesFailed";
        case response_code::error_address_space_not_found:
            return "ErrorAddressSpaceNotFound";
        case response_code::error_ad_operation:
            return "ErrorADOperation";
        case response_code::error_ad_session_filter:
            return "ErrorADSessionFilter";
        case response_code::error_ad_unavailable:
            return "ErrorADUnavailable";
        case response_code::error_affected_task_occurrences_required:
            return "ErrorAffectedTaskOccurrencesRequired";
        case response_code::error_archive_folder_path_creation:
            return "ErrorArchiveFolderPathCreation";
        case response_code::error_archive_mailbox_not_enabled:
            return "ErrorArchiveMailboxNotEnabled";
        case response_code::error_archive_mailbox_service_discovery_failed:
            return "ErrorArchiveMailboxServiceDiscoveryFailed";
        case response_code::error_attachment_nest_level_limit_exceeded:
            return "ErrorAttachmentNestLevelLimitExceeded";
        case response_code::error_attachment_size_limit_exceeded:
            return "ErrorAttachmentSizeLimitExceeded";
        case response_code::error_auto_discover_failed:
            return "ErrorAutoDiscoverFailed";
        case response_code::error_availability_config_not_found:
            return "ErrorAvailabilityConfigNotFound";
        case response_code::error_batch_processing_stopped:
            return "ErrorBatchProcessingStopped";
        case response_code::error_calendar_cannot_move_or_copy_occurrence:
            return "ErrorCalendarCannotMoveOrCopyOccurrence";
        case response_code::error_calendar_cannot_update_deleted_item:
            return "ErrorCalendarCannotUpdateDeletedItem";
        case response_code::error_calendar_cannot_use_id_for_occurrence_id:
            return "ErrorCalendarCannotUseIdForOccurrenceId";
        case response_code::
            error_calendar_cannot_use_id_for_recurring_master_id:
            return "ErrorCalendarCannotUseIdForRecurringMasterId";
        case response_code::error_calendar_duration_is_too_long:
            return "ErrorCalendarDurationIsTooLong";
        case response_code::error_calendar_end_date_is_earlier_than_start_date:
            return "ErrorCalendarEndDateIsEarlierThanStartDate";
        case response_code::error_calendar_folder_is_invalid_for_calendar_view:
            return "ErrorCalendarFolderIsInvalidForCalendarView";
        case response_code::error_calendar_invalid_attribute_value:
            return "ErrorCalendarInvalidAttributeValue";
        case response_code::error_calendar_invalid_day_for_time_change_pattern:
            return "ErrorCalendarInvalidDayForTimeChangePattern";
        case response_code::error_calendar_invalid_day_for_weekly_recurrence:
            return "ErrorCalendarInvalidDayForWeeklyRecurrence";
        case response_code::error_calendar_invalid_property_state:
            return "ErrorCalendarInvalidPropertyState";
        case response_code::error_calendar_invalid_property_value:
            return "ErrorCalendarInvalidPropertyValue";
        case response_code::error_calendar_invalid_recurrence:
            return "ErrorCalendarInvalidRecurrence";
        case response_code::error_calendar_invalid_time_zone:
            return "ErrorCalendarInvalidTimeZone";
        case response_code::error_calendar_is_cancelled_for_accept:
            return "ErrorCalendarIsCancelledForAccept";
        case response_code::error_calendar_is_cancelled_for_decline:
            return "ErrorCalendarIsCancelledForDecline";
        case response_code::error_calendar_is_cancelled_for_remove:
            return "ErrorCalendarIsCancelledForRemove";
        case response_code::error_calendar_is_cancelled_for_tentative:
            return "ErrorCalendarIsCancelledForTentative";
        case response_code::error_calendar_is_delegated_for_accept:
            return "ErrorCalendarIsDelegatedForAccept";
        case response_code::error_calendar_is_delegated_for_decline:
            return "ErrorCalendarIsDelegatedForDecline";
        case response_code::error_calendar_is_delegated_for_remove:
            return "ErrorCalendarIsDelegatedForRemove";
        case response_code::error_calendar_is_delegated_for_tentative:
            return "ErrorCalendarIsDelegatedForTentative";
        case response_code::error_calendar_is_not_organizer:
            return "ErrorCalendarIsNotOrganizer";
        case response_code::error_calendar_is_organizer_for_accept:
            return "ErrorCalendarIsOrganizerForAccept";
        case response_code::error_calendar_is_organizer_for_decline:
            return "ErrorCalendarIsOrganizerForDecline";
        case response_code::error_calendar_is_organizer_for_remove:
            return "ErrorCalendarIsOrganizerForRemove";
        case response_code::error_calendar_is_organizer_for_tentative:
            return "ErrorCalendarIsOrganizerForTentative";
        case response_code::error_calendar_meeting_request_is_out_of_date:
            return "ErrorCalendarMeetingRequestIsOutOfDate";
        case response_code::
            error_calendar_occurrence_index_is_out_of_recurrence_range:
            return "ErrorCalendarOccurrenceIndexIsOutOfRecurrenceRange";
        case response_code::
            error_calendar_occurrence_is_deleted_from_recurrence:
            return "ErrorCalendarOccurrenceIsDeletedFromRecurrence";
        case response_code::error_calendar_out_of_range:
            return "ErrorCalendarOutOfRange";
        case response_code::error_calendar_view_range_too_big:
            return "ErrorCalendarViewRangeTooBig";
        case response_code::error_caller_is_invalid_ad_account:
            return "ErrorCallerIsInvalidADAccount";
        case response_code::
            error_cannot_archive_calendar_contact_task_folder_exception:
            return "ErrorCannotArchiveCalendarContactTaskFolderException";
        case response_code::error_cannot_archive_items_in_public_folders:
            return "ErrorCannotArchiveItemsInPublicFolders";
        case response_code::error_cannot_archive_items_in_archive_mailbox:
            return "ErrorCannotArchiveItemsInArchiveMailbox";
        case response_code::
            error_cannot_create_calendar_item_in_non_calendar_folder:
            return "ErrorCannotCreateCalendarItemInNonCalendarFolder";
        case response_code::error_cannot_create_contact_in_non_contact_folder:
            return "ErrorCannotCreateContactInNonContactFolder";
        case response_code::error_cannot_create_post_item_in_non_mail_folder:
            return "ErrorCannotCreatePostItemInNonMailFolder";
        case response_code::error_cannot_create_task_in_non_task_folder:
            return "ErrorCannotCreateTaskInNonTaskFolder";
        case response_code::error_cannot_delete_object:
            return "ErrorCannotDeleteObject";
        case response_code::error_cannot_delete_task_occurrence:
            return "ErrorCannotDeleteTaskOccurrence";
        case response_code::error_cannot_disable_mandatory_extension:
            return "ErrorCannotDisableMandatoryExtension";
        case response_code::error_cannot_empty_folder:
            return "ErrorCannotEmptyFolder";
        case response_code::error_cannot_get_source_folder_path:
            return "ErrorCannotGetSourceFolderPath";
        case response_code::error_cannot_get_external_ecp_url:
            return "ErrorCannotGetExternalEcpUrl";
        case response_code::error_cannot_open_file_attachment:
            return "ErrorCannotOpenFileAttachment";
        case response_code::
            error_cannot_set_calendar_permission_on_non_calendar_folder:
            return "ErrorCannotSetCalendarPermissionOnNonCalendarFolder";
        case response_code::
            error_cannot_set_non_calendar_permission_on_calendar_folder:
            return "ErrorCannotSetNonCalendarPermissionOnCalendarFolder";
        case response_code::error_cannot_set_permission_unknown_entries:
            return "ErrorCannotSetPermissionUnknownEntries";
        case response_code::error_cannot_specify_search_folder_as_source_folder:
            return "ErrorCannotSpecifySearchFolderAsSourceFolder";
        case response_code::error_cannot_use_folder_id_for_item_id:
            return "ErrorCannotUseFolderIdForItemId";
        case response_code::error_cannot_use_item_id_for_folder_id:
            return "ErrorCannotUseItemIdForFolderId";
        case response_code::error_change_key_required:
            return "ErrorChangeKeyRequired";
        case response_code::error_change_key_required_for_write_operations:
            return "ErrorChangeKeyRequiredForWriteOperations";
        case response_code::error_client_disconnected:
            return "ErrorClientDisconnected";
        case response_code::error_client_intent_invalid_state_definition:
            return "ErrorClientIntentInvalidStateDefinition";
        case response_code::error_client_intent_not_found:
            return "ErrorClientIntentNotFound";
        case response_code::error_connection_failed:
            return "ErrorConnectionFailed";
        case response_code::error_contains_filter_wrong_type:
            return "ErrorContainsFilterWrongType";
        case response_code::error_content_conversion_failed:
            return "ErrorContentConversionFailed";
        case response_code::error_content_indexing_not_enabled:
            return "ErrorContentIndexingNotEnabled";
        case response_code::error_corrupt_data:
            return "ErrorCorruptData";
        case response_code::error_create_item_access_denied:
            return "ErrorCreateItemAccessDenied";
        case response_code::error_create_managed_folder_partial_completion:
            return "ErrorCreateManagedFolderPartialCompletion";
        case response_code::error_create_subfolder_access_denied:
            return "ErrorCreateSubfolderAccessDenied";
        case response_code::error_cross_mailbox_move_copy:
            return "ErrorCrossMailboxMoveCopy";
        case response_code::error_cross_site_request:
            return "ErrorCrossSiteRequest";
        case response_code::error_data_size_limit_exceeded:
            return "ErrorDataSizeLimitExceeded";
        case response_code::error_data_source_operation:
            return "ErrorDataSourceOperation";
        case response_code::error_delegate_already_exists:
            return "ErrorDelegateAlreadyExists";
        case response_code::error_delegate_cannot_add_owner:
            return "ErrorDelegateCannotAddOwner";
        case response_code::error_delegate_missing_configuration:
            return "ErrorDelegateMissingConfiguration";
        case response_code::error_delegate_no_user:
            return "ErrorDelegateNoUser";
        case response_code::error_delegate_validation_failed:
            return "ErrorDelegateValidationFailed";
        case response_code::error_delete_distinguished_folder:
            return "ErrorDeleteDistinguishedFolder";
        case response_code::error_delete_items_failed:
            return "ErrorDeleteItemsFailed";
        case response_code::error_delete_unified_messaging_prompt_failed:
            return "ErrorDeleteUnifiedMessagingPromptFailed";
        case response_code::error_distinguished_user_not_supported:
            return "ErrorDistinguishedUserNotSupported";
        case response_code::error_distribution_list_member_not_exist:
            return "ErrorDistributionListMemberNotExist";
        case response_code::error_duplicate_input_folder_names:
            return "ErrorDuplicateInputFolderNames";
        case response_code::error_duplicate_user_ids_specified:
            return "ErrorDuplicateUserIdsSpecified";
        case response_code::error_email_address_mismatch:
            return "ErrorEmailAddressMismatch";
        case response_code::error_event_not_found:
            return "ErrorEventNotFound";
        case response_code::error_exceeded_connection_count:
            return "ErrorExceededConnectionCount";
        case response_code::error_exceeded_subscription_count:
            return "ErrorExceededSubscriptionCount";
        case response_code::error_exceeded_find_count_limit:
            return "ErrorExceededFindCountLimit";
        case response_code::error_expired_subscription:
            return "ErrorExpiredSubscription";
        case response_code::error_extension_not_found:
            return "ErrorExtensionNotFound";
        case response_code::error_folder_corrupt:
            return "ErrorFolderCorrupt";
        case response_code::error_folder_exists:
            return "ErrorFolderExists";
        case response_code::error_folder_not_found:
            return "ErrorFolderNotFound";
        case response_code::error_folder_property_request_failed:
            return "ErrorFolderPropertyRequestFailed";
        case response_code::error_folder_save:
            return "ErrorFolderSave";
        case response_code::error_folder_save_failed:
            return "ErrorFolderSaveFailed";
        case response_code::error_folder_save_property_error:
            return "ErrorFolderSavePropertyError";
        case response_code::error_free_busy_generation_failed:
            return "ErrorFreeBusyGenerationFailed";
        case response_code::error_get_server_security_descriptor_failed:
            return "ErrorGetServerSecurityDescriptorFailed";
        case response_code::error_im_contact_limit_reached:
            return "ErrorImContactLimitReached";
        case response_code::error_im_group_display_name_already_exists:
            return "ErrorImGroupDisplayNameAlreadyExists";
        case response_code::error_im_group_limit_reached:
            return "ErrorImGroupLimitReached";
        case response_code::error_impersonate_user_denied:
            return "ErrorImpersonateUserDenied";
        case response_code::error_impersonation_denied:
            return "ErrorImpersonationDenied";
        case response_code::error_impersonation_failed:
            return "ErrorImpersonationFailed";
        case response_code::error_incorrect_schema_version:
            return "ErrorIncorrectSchemaVersion";
        case response_code::error_incorrect_update_property_count:
            return "ErrorIncorrectUpdatePropertyCount";
        case response_code::error_individual_mailbox_limit_reached:
            return "ErrorIndividualMailboxLimitReached";
        case response_code::error_insufficient_resources:
            return "ErrorInsufficientResources";
        case response_code::error_internal_server_error:
            return "ErrorInternalServerError";
        case response_code::error_internal_server_transient_error:
            return "ErrorInternalServerTransientError";
        case response_code::error_invalid_access_level:
            return "ErrorInvalidAccessLevel";
        case response_code::error_invalid_argument:
            return "ErrorInvalidArgument";
        case response_code::error_invalid_attachment_id:
            return "ErrorInvalidAttachmentId";
        case response_code::error_invalid_attachment_subfilter:
            return "ErrorInvalidAttachmentSubfilter";
        case response_code::error_invalid_attachment_subfilter_text_filter:
            return "ErrorInvalidAttachmentSubfilterTextFilter";
        case response_code::error_invalid_authorization_context:
            return "ErrorInvalidAuthorizationContext";
        case response_code::error_invalid_change_key:
            return "ErrorInvalidChangeKey";
        case response_code::error_invalid_client_security_context:
            return "ErrorInvalidClientSecurityContext";
        case response_code::error_invalid_complete_date:
            return "ErrorInvalidCompleteDate";
        case response_code::error_invalid_contact_email_address:
            return "ErrorInvalidContactEmailAddress";
        case response_code::error_invalid_contact_email_index:
            return "ErrorInvalidContactEmailIndex";
        case response_code::error_invalid_cross_forest_credentials:
            return "ErrorInvalidCrossForestCredentials";
        case response_code::error_invalid_delegate_permission:
            return "ErrorInvalidDelegatePermission";
        case response_code::error_invalid_delegate_user_id:
            return "ErrorInvalidDelegateUserId";
        case response_code::error_invalid_exchange_impersonation_header_data:
            return "ErrorInvalidExchangeImpersonationHeaderData";
        case response_code::error_invalid_excludes_restriction:
            return "ErrorInvalidExcludesRestriction";
        case response_code::error_invalid_expression_type_for_sub_filter:
            return "ErrorInvalidExpressionTypeForSubFilter";
        case response_code::error_invalid_extended_property:
            return "ErrorInvalidExtendedProperty";
        case response_code::error_invalid_extended_property_value:
            return "ErrorInvalidExtendedPropertyValue";
        case response_code::error_invalid_external_sharing_initiator:
            return "ErrorInvalidExternalSharingInitiator";
        case response_code::error_invalid_external_sharing_subscriber:
            return "ErrorInvalidExternalSharingSubscriber";
        case response_code::error_invalid_federated_organization_id:
            return "ErrorInvalidFederatedOrganizationId";
        case response_code::error_invalid_folder_id:
            return "ErrorInvalidFolderId";
        case response_code::error_invalid_folder_type_for_operation:
            return "ErrorInvalidFolderTypeForOperation";
        case response_code::error_invalid_fractional_paging_parameters:
            return "ErrorInvalidFractionalPagingParameters";
        case response_code::error_invalid_free_busy_view_type:
            return "ErrorInvalidFreeBusyViewType";
        case response_code::error_invalid_id:
            return "ErrorInvalidId";
        case response_code::error_invalid_id_empty:
            return "ErrorInvalidIdEmpty";
        case response_code::error_invalid_like_request:
            return "ErrorInvalidLikeRequest";
        case response_code::error_invalid_id_malformed:
            return "ErrorInvalidIdMalformed";
        case response_code::error_invalid_id_malformed_ews_legacy_id_format:
            return "ErrorInvalidIdMalformedEwsLegacyIdFormat";
        case response_code::error_invalid_id_moniker_too_long:
            return "ErrorInvalidIdMonikerTooLong";
        case response_code::error_invalid_id_not_an_item_attachment_id:
            return "ErrorInvalidIdNotAnItemAttachmentId";
        case response_code::error_invalid_id_returned_by_resolve_names:
            return "ErrorInvalidIdReturnedByResolveNames";
        case response_code::error_invalid_id_store_object_id_too_long:
            return "ErrorInvalidIdStoreObjectIdTooLong";
        case response_code::error_invalid_id_too_many_attachment_levels:
            return "ErrorInvalidIdTooManyAttachmentLevels";
        case response_code::error_invalid_id_xml:
            return "ErrorInvalidIdXml";
        case response_code::error_invalid_im_contact_id:
            return "ErrorInvalidImContactId";
        case response_code::error_invalid_im_distribution_group_smtp_address:
            return "ErrorInvalidImDistributionGroupSmtpAddress";
        case response_code::error_invalid_im_group_id:
            return "ErrorInvalidImGroupId";
        case response_code::error_invalid_indexed_paging_parameters:
            return "ErrorInvalidIndexedPagingParameters";
        case response_code::error_invalid_internet_header_child_nodes:
            return "ErrorInvalidInternetHeaderChildNodes";
        case response_code::error_invalid_item_for_operation_archive_item:
            return "ErrorInvalidItemForOperationArchiveItem";
        case response_code::error_invalid_item_for_operation_accept_item:
            return "ErrorInvalidItemForOperationAcceptItem";
        case response_code::error_invalid_item_for_operation_cancel_item:
            return "ErrorInvalidItemForOperationCancelItem";
        case response_code::
            error_invalid_item_for_operation_create_item_attachment:
            return "ErrorInvalidItemForOperationCreateItemAttachment";
        case response_code::error_invalid_item_for_operation_create_item:
            return "ErrorInvalidItemForOperationCreateItem";
        case response_code::error_invalid_item_for_operation_decline_item:
            return "ErrorInvalidItemForOperationDeclineItem";
        case response_code::error_invalid_item_for_operation_expand_dl:
            return "ErrorInvalidItemForOperationExpandDL";
        case response_code::error_invalid_item_for_operation_remove_item:
            return "ErrorInvalidItemForOperationRemoveItem";
        case response_code::error_invalid_item_for_operation_send_item:
            return "ErrorInvalidItemForOperationSendItem";
        case response_code::error_invalid_item_for_operation_tentative:
            return "ErrorInvalidItemForOperationTentative";
        case response_code::error_invalid_logon_type:
            return "ErrorInvalidLogonType";
        case response_code::error_invalid_mailbox:
            return "ErrorInvalidMailbox";
        case response_code::error_invalid_managed_folder_property:
            return "ErrorInvalidManagedFolderProperty";
        case response_code::error_invalid_managed_folder_quota:
            return "ErrorInvalidManagedFolderQuota";
        case response_code::error_invalid_managed_folder_size:
            return "ErrorInvalidManagedFolderSize";
        case response_code::error_invalid_merged_free_busy_interval:
            return "ErrorInvalidMergedFreeBusyInterval";
        case response_code::error_invalid_name_for_name_resolution:
            return "ErrorInvalidNameForNameResolution";
        case response_code::error_invalid_network_service_context:
            return "ErrorInvalidNetworkServiceContext";
        case response_code::error_invalid_oof_parameter:
            return "ErrorInvalidOofParameter";
        case response_code::error_invalid_operation:
            return "ErrorInvalidOperation";
        case response_code::
            error_invalid_organization_relationship_for_free_busy:
            return "ErrorInvalidOrganizationRelationshipForFreeBusy";
        case response_code::error_invalid_paging_max_rows:
            return "ErrorInvalidPagingMaxRows";
        case response_code::error_invalid_parent_folder:
            return "ErrorInvalidParentFolder";
        case response_code::error_invalid_percent_complete_value:
            return "ErrorInvalidPercentCompleteValue";
        case response_code::error_invalid_permission_settings:
            return "ErrorInvalidPermissionSettings";
        case response_code::error_invalid_phone_call_id:
            return "ErrorInvalidPhoneCallId";
        case response_code::error_invalid_phone_number:
            return "ErrorInvalidPhoneNumber";
        case response_code::error_invalid_property_append:
            return "ErrorInvalidPropertyAppend";
        case response_code::error_invalid_property_delete:
            return "ErrorInvalidPropertyDelete";
        case response_code::error_invalid_property_for_exists:
            return "ErrorInvalidPropertyForExists";
        case response_code::error_invalid_property_for_operation:
            return "ErrorInvalidPropertyForOperation";
        case response_code::error_invalid_property_request:
            return "ErrorInvalidPropertyRequest";
        case response_code::error_invalid_property_set:
            return "ErrorInvalidPropertySet";
        case response_code::error_invalid_property_update_sent_message:
            return "ErrorInvalidPropertyUpdateSentMessage";
        case response_code::error_invalid_proxy_security_context:
            return "ErrorInvalidProxySecurityContext";
        case response_code::error_invalid_pull_subscription_id:
            return "ErrorInvalidPullSubscriptionId";
        case response_code::error_invalid_push_subscription_url:
            return "ErrorInvalidPushSubscriptionUrl";
        case response_code::error_invalid_recipients:
            return "ErrorInvalidRecipients";
        case response_code::error_invalid_recipient_subfilter:
            return "ErrorInvalidRecipientSubfilter";
        case response_code::error_invalid_recipient_subfilter_comparison:
            return "ErrorInvalidRecipientSubfilterComparison";
        case response_code::error_invalid_recipient_subfilter_order:
            return "ErrorInvalidRecipientSubfilterOrder";
        case response_code::error_invalid_recipient_subfilter_text_filter:
            return "ErrorInvalidRecipientSubfilterTextFilter";
        case response_code::error_invalid_reference_item:
            return "ErrorInvalidReferenceItem";
        case response_code::error_invalid_request:
            return "ErrorInvalidRequest";
        case response_code::error_invalid_restriction:
            return "ErrorInvalidRestriction";
        case response_code::error_invalid_retention_tag_type_mismatch:
            return "ErrorInvalidRetentionTagTypeMismatch";
        case response_code::error_invalid_retention_tag_invisible:
            return "ErrorInvalidRetentionTagInvisible";
        case response_code::error_invalid_retention_tag_inheritance:
            return "ErrorInvalidRetentionTagInheritance";
        case response_code::error_invalid_retention_tag_id_guid:
            return "ErrorInvalidRetentionTagIdGuid";
        case response_code::error_invalid_routing_type:
            return "ErrorInvalidRoutingType";
        case response_code::error_invalid_scheduled_oof_duration:
            return "ErrorInvalidScheduledOofDuration";
        case response_code::error_invalid_schema_version_for_mailbox_version:
            return "ErrorInvalidSchemaVersionForMailboxVersion";
        case response_code::error_invalid_security_descriptor:
            return "ErrorInvalidSecurityDescriptor";
        case response_code::error_invalid_send_item_save_settings:
            return "ErrorInvalidSendItemSaveSettings";
        case response_code::error_invalid_serialized_access_token:
            return "ErrorInvalidSerializedAccessToken";
        case response_code::error_invalid_sharing_data:
            return "ErrorInvalidSharingData";
        case response_code::error_invalid_sharing_message:
            return "ErrorInvalidSharingMessage";
        case response_code::error_invalid_sid:
            return "ErrorInvalidSid";
        case response_code::error_invalid_sip_uri:
            return "ErrorInvalidSIPUri";
        case response_code::error_invalid_server_version:
            return "ErrorInvalidServerVersion";
        case response_code::error_invalid_smtp_address:
            return "ErrorInvalidSmtpAddress";
        case response_code::error_invalid_subfilter_type:
            return "ErrorInvalidSubfilterType";
        case response_code::error_invalid_subfilter_type_not_attendee_type:
            return "ErrorInvalidSubfilterTypeNotAttendeeType";
        case response_code::error_invalid_subfilter_type_not_recipient_type:
            return "ErrorInvalidSubfilterTypeNotRecipientType";
        case response_code::error_invalid_subscription:
            return "ErrorInvalidSubscription";
        case response_code::error_invalid_subscription_request:
            return "ErrorInvalidSubscriptionRequest";
        case response_code::error_invalid_sync_state_data:
            return "ErrorInvalidSyncStateData";
        case response_code::error_invalid_time_interval:
            return "ErrorInvalidTimeInterval";
        case response_code::error_invalid_user_info:
            return "ErrorInvalidUserInfo";
        case response_code::error_invalid_user_oof_settings:
            return "ErrorInvalidUserOofSettings";
        case response_code::error_invalid_user_principal_name:
            return "ErrorInvalidUserPrincipalName";
        case response_code::error_invalid_user_sid:
            return "ErrorInvalidUserSid";
        case response_code::error_invalid_user_sid_missing_upn:
            return "ErrorInvalidUserSidMissingUPN";
        case response_code::error_invalid_value_for_property:
            return "ErrorInvalidValueForProperty";
        case response_code::error_invalid_watermark:
            return "ErrorInvalidWatermark";
        case response_code::error_ip_gateway_not_found:
            return "ErrorIPGatewayNotFound";
        case response_code::error_irresolvable_conflict:
            return "ErrorIrresolvableConflict";
        case response_code::error_item_corrupt:
            return "ErrorItemCorrupt";
        case response_code::error_item_not_found:
            return "ErrorItemNotFound";
        case response_code::error_item_property_request_failed:
            return "ErrorItemPropertyRequestFailed";
        case response_code::error_item_save:
            return "ErrorItemSave";
        case response_code::error_item_save_property_error:
            return "ErrorItemSavePropertyError";
        case response_code::error_legacy_mailbox_free_busy_view_type_not_merged:
            return "ErrorLegacyMailboxFreeBusyViewTypeNotMerged";
        case response_code::error_local_server_object_not_found:
            return "ErrorLocalServerObjectNotFound";
        case response_code::error_logon_as_network_service_failed:
            return "ErrorLogonAsNetworkServiceFailed";
        case response_code::error_mailbox_configuration:
            return "ErrorMailboxConfiguration";
        case response_code::error_mailbox_data_array_empty:
            return "ErrorMailboxDataArrayEmpty";
        case response_code::error_mailbox_data_array_too_big:
            return "ErrorMailboxDataArrayTooBig";
        case response_code::error_mailbox_failover:
            return "ErrorMailboxFailover";
        case response_code::error_mailbox_hold_not_found:
            return "ErrorMailboxHoldNotFound";
        case response_code::error_mailbox_logon_failed:
            return "ErrorMailboxLogonFailed";
        case response_code::error_mailbox_move_in_progress:
            return "ErrorMailboxMoveInProgress";
        case response_code::error_mailbox_store_unavailable:
            return "ErrorMailboxStoreUnavailable";
        case response_code::error_mail_recipient_not_found:
            return "ErrorMailRecipientNotFound";
        case response_code::error_mail_tips_disabled:
            return "ErrorMailTipsDisabled";
        case response_code::error_managed_folder_already_exists:
            return "ErrorManagedFolderAlreadyExists";
        case response_code::error_managed_folder_not_found:
            return "ErrorManagedFolderNotFound";
        case response_code::error_managed_folders_root_failure:
            return "ErrorManagedFoldersRootFailure";
        case response_code::error_meeting_suggestion_generation_failed:
            return "ErrorMeetingSuggestionGenerationFailed";
        case response_code::error_message_disposition_required:
            return "ErrorMessageDispositionRequired";
        case response_code::error_message_size_exceeded:
            return "ErrorMessageSizeExceeded";
        case response_code::error_message_tracking_no_such_domain:
            return "ErrorMessageTrackingNoSuchDomain";
        case response_code::error_message_tracking_permanent_error:
            return "ErrorMessageTrackingPermanentError";
        case response_code::_error_message_tracking_transient_error:
            return " ErrorMessageTrackingTransientError";
        case response_code::error_mime_content_conversion_failed:
            return "ErrorMimeContentConversionFailed";
        case response_code::error_mime_content_invalid:
            return "ErrorMimeContentInvalid";
        case response_code::error_mime_content_invalid_base64_string:
            return "ErrorMimeContentInvalidBase64String";
        case response_code::error_missing_argument:
            return "ErrorMissingArgument";
        case response_code::error_missing_email_address:
            return "ErrorMissingEmailAddress";
        case response_code::error_missing_email_address_for_managed_folder:
            return "ErrorMissingEmailAddressForManagedFolder";
        case response_code::error_missing_information_email_address:
            return "ErrorMissingInformationEmailAddress";
        case response_code::error_missing_information_reference_item_id:
            return "ErrorMissingInformationReferenceItemId";
        case response_code::error_missing_item_for_create_item_attachment:
            return "ErrorMissingItemForCreateItemAttachment";
        case response_code::error_missing_managed_folder_id:
            return "ErrorMissingManagedFolderId";
        case response_code::error_missing_recipients:
            return "ErrorMissingRecipients";
        case response_code::error_missing_user_id_information:
            return "ErrorMissingUserIdInformation";
        case response_code::error_more_than_one_access_mode_specified:
            return "ErrorMoreThanOneAccessModeSpecified";
        case response_code::error_move_copy_failed:
            return "ErrorMoveCopyFailed";
        case response_code::error_move_distinguished_folder:
            return "ErrorMoveDistinguishedFolder";
        case response_code::error_multi_legacy_mailbox_access:
            return "ErrorMultiLegacyMailboxAccess";
        case response_code::error_name_resolution_multiple_results:
            return "ErrorNameResolutionMultipleResults";
        case response_code::error_name_resolution_no_mailbox:
            return "ErrorNameResolutionNoMailbox";
        case response_code::error_name_resolution_no_results:
            return "ErrorNameResolutionNoResults";
        case response_code::error_no_applicable_proxy_cas_servers_available:
            return "ErrorNoApplicableProxyCASServersAvailable";
        case response_code::error_no_calendar:
            return "ErrorNoCalendar";
        case response_code::
            error_no_destination_cas_due_to_kerberos_requirements:
            return "ErrorNoDestinationCASDueToKerberosRequirements";
        case response_code::error_no_destination_cas_due_to_ssl_requirements:
            return "ErrorNoDestinationCASDueToSSLRequirements";
        case response_code::error_no_destination_cas_due_to_version_mismatch:
            return "ErrorNoDestinationCASDueToVersionMismatch";
        case response_code::error_no_folder_class_override:
            return "ErrorNoFolderClassOverride";
        case response_code::error_no_free_busy_access:
            return "ErrorNoFreeBusyAccess";
        case response_code::error_non_existent_mailbox:
            return "ErrorNonExistentMailbox";
        case response_code::error_non_primary_smtp_address:
            return "ErrorNonPrimarySmtpAddress";
        case response_code::error_no_property_tag_for_custom_properties:
            return "ErrorNoPropertyTagForCustomProperties";
        case response_code::error_no_public_folder_replica_available:
            return "ErrorNoPublicFolderReplicaAvailable";
        case response_code::error_no_public_folder_server_available:
            return "ErrorNoPublicFolderServerAvailable";
        case response_code::error_no_responding_cas_in_destination_site:
            return "ErrorNoRespondingCASInDestinationSite";
        case response_code::error_not_delegate:
            return "ErrorNotDelegate";
        case response_code::error_not_enough_memory:
            return "ErrorNotEnoughMemory";
        case response_code::error_not_supported_sharing_message:
            return "ErrorNotSupportedSharingMessage";
        case response_code::error_object_type_changed:
            return "ErrorObjectTypeChanged";
        case response_code::error_occurrence_crossing_boundary:
            return "ErrorOccurrenceCrossingBoundary";
        case response_code::error_occurrence_time_span_too_big:
            return "ErrorOccurrenceTimeSpanTooBig";
        case response_code::error_operation_not_allowed_with_public_folder_root:
            return "ErrorOperationNotAllowedWithPublicFolderRoot";
        case response_code::error_organization_not_federated:
            return "ErrorOrganizationNotFederated";
        case response_code::error_parent_folder_id_required:
            return "ErrorParentFolderIdRequired";
        case response_code::error_parent_folder_not_found:
            return "ErrorParentFolderNotFound";
        case response_code::error_password_change_required:
            return "ErrorPasswordChangeRequired";
        case response_code::error_password_expired:
            return "ErrorPasswordExpired";
        case response_code::error_phone_number_not_dialable:
            return "ErrorPhoneNumberNotDialable";
        case response_code::error_property_update:
            return "ErrorPropertyUpdate";
        case response_code::error_prompt_publishing_operation_failed:
            return "ErrorPromptPublishingOperationFailed";
        case response_code::error_property_validation_failure:
            return "ErrorPropertyValidationFailure";
        case response_code::error_proxied_subscription_call_failure:
            return "ErrorProxiedSubscriptionCallFailure";
        case response_code::error_proxy_call_failed:
            return "ErrorProxyCallFailed";
        case response_code::error_proxy_group_sid_limit_exceeded:
            return "ErrorProxyGroupSidLimitExceeded";
        case response_code::error_proxy_request_not_allowed:
            return "ErrorProxyRequestNotAllowed";
        case response_code::error_proxy_request_processing_failed:
            return "ErrorProxyRequestProcessingFailed";
        case response_code::error_proxy_service_discovery_failed:
            return "ErrorProxyServiceDiscoveryFailed";
        case response_code::error_proxy_token_expired:
            return "ErrorProxyTokenExpired";
        case response_code::error_public_folder_mailbox_discovery_failed:
            return "ErrorPublicFolderMailboxDiscoveryFailed";
        case response_code::error_public_folder_operation_failed:
            return "ErrorPublicFolderOperationFailed";
        case response_code::error_public_folder_request_processing_failed:
            return "ErrorPublicFolderRequestProcessingFailed";
        case response_code::error_public_folder_server_not_found:
            return "ErrorPublicFolderServerNotFound";
        case response_code::error_public_folder_sync_exception:
            return "ErrorPublicFolderSyncException";
        case response_code::error_query_filter_too_long:
            return "ErrorQueryFilterTooLong";
        case response_code::error_quota_exceeded:
            return "ErrorQuotaExceeded";
        case response_code::error_read_events_failed:
            return "ErrorReadEventsFailed";
        case response_code::error_read_receipt_not_pending:
            return "ErrorReadReceiptNotPending";
        case response_code::error_recurrence_end_date_too_big:
            return "ErrorRecurrenceEndDateTooBig";
        case response_code::error_recurrence_has_no_occurrence:
            return "ErrorRecurrenceHasNoOccurrence";
        case response_code::error_remove_delegates_failed:
            return "ErrorRemoveDelegatesFailed";
        case response_code::error_request_aborted:
            return "ErrorRequestAborted";
        case response_code::error_request_stream_too_big:
            return "ErrorRequestStreamTooBig";
        case response_code::error_required_property_missing:
            return "ErrorRequiredPropertyMissing";
        case response_code::error_resolve_names_invalid_folder_type:
            return "ErrorResolveNamesInvalidFolderType";
        case response_code::
            error_resolve_names_only_one_contacts_folder_allowed:
            return "ErrorResolveNamesOnlyOneContactsFolderAllowed";
        case response_code::error_response_schema_validation:
            return "ErrorResponseSchemaValidation";
        case response_code::error_restriction_too_long:
            return "ErrorRestrictionTooLong";
        case response_code::error_restriction_too_complex:
            return "ErrorRestrictionTooComplex";
        case response_code::error_result_set_too_big:
            return "ErrorResultSetTooBig";
        case response_code::error_saved_item_folder_not_found:
            return "ErrorSavedItemFolderNotFound";
        case response_code::error_schema_validation:
            return "ErrorSchemaValidation";
        case response_code::error_search_folder_not_initialized:
            return "ErrorSearchFolderNotInitialized";
        case response_code::error_send_as_denied:
            return "ErrorSendAsDenied";
        case response_code::error_send_meeting_cancellations_required:
            return "ErrorSendMeetingCancellationsRequired";
        case response_code::
            error_send_meeting_invitations_or_cancellations_required:
            return "ErrorSendMeetingInvitationsOrCancellationsRequired";
        case response_code::error_send_meeting_invitations_required:
            return "ErrorSendMeetingInvitationsRequired";
        case response_code::error_sent_meeting_request_update:
            return "ErrorSentMeetingRequestUpdate";
        case response_code::error_sent_task_request_update:
            return "ErrorSentTaskRequestUpdate";
        case response_code::error_server_busy:
            return "ErrorServerBusy";
        case response_code::error_service_discovery_failed:
            return "ErrorServiceDiscoveryFailed";
        case response_code::error_stale_object:
            return "ErrorStaleObject";
        case response_code::error_submission_quota_exceeded:
            return "ErrorSubmissionQuotaExceeded";
        case response_code::error_subscription_access_denied:
            return "ErrorSubscriptionAccessDenied";
        case response_code::error_subscription_delegate_access_not_supported:
            return "ErrorSubscriptionDelegateAccessNotSupported";
        case response_code::error_subscription_not_found:
            return "ErrorSubscriptionNotFound";
        case response_code::error_subscription_unsubscribed:
            return "ErrorSubscriptionUnsubscribed";
        case response_code::error_sync_folder_not_found:
            return "ErrorSyncFolderNotFound";
        case response_code::error_team_mailbox_not_found:
            return "ErrorTeamMailboxNotFound";
        case response_code::error_team_mailbox_not_linked_to_share_point:
            return "ErrorTeamMailboxNotLinkedToSharePoint";
        case response_code::error_team_mailbox_url_validation_failed:
            return "ErrorTeamMailboxUrlValidationFailed";
        case response_code::error_team_mailbox_not_authorized_owner:
            return "ErrorTeamMailboxNotAuthorizedOwner";
        case response_code::error_team_mailbox_active_to_pending_delete:
            return "ErrorTeamMailboxActiveToPendingDelete";
        case response_code::error_team_mailbox_failed_sending_notifications:
            return "ErrorTeamMailboxFailedSendingNotifications";
        case response_code::error_team_mailbox_error_unknown:
            return "ErrorTeamMailboxErrorUnknown";
        case response_code::error_time_interval_too_big:
            return "ErrorTimeIntervalTooBig";
        case response_code::error_timeout_expired:
            return "ErrorTimeoutExpired";
        case response_code::error_time_zone:
            return "ErrorTimeZone";
        case response_code::error_to_folder_not_found:
            return "ErrorToFolderNotFound";
        case response_code::error_token_serialization_denied:
            return "ErrorTokenSerializationDenied";
        case response_code::error_too_many_objects_opened:
            return "ErrorTooManyObjectsOpened";
        case response_code::error_unified_messaging_dial_plan_not_found:
            return "ErrorUnifiedMessagingDialPlanNotFound";
        case response_code::error_unified_messaging_report_data_not_found:
            return "ErrorUnifiedMessagingReportDataNotFound";
        case response_code::error_unified_messaging_prompt_not_found:
            return "ErrorUnifiedMessagingPromptNotFound";
        case response_code::error_unified_messaging_request_failed:
            return "ErrorUnifiedMessagingRequestFailed";
        case response_code::error_unified_messaging_server_not_found:
            return "ErrorUnifiedMessagingServerNotFound";
        case response_code::error_unable_to_get_user_oof_settings:
            return "ErrorUnableToGetUserOofSettings";
        case response_code::error_unable_to_remove_im_contact_from_group:
            return "ErrorUnableToRemoveImContactFromGroup";
        case response_code::error_unsupported_culture:
            return "ErrorUnsupportedCulture";
        case response_code::error_unsupported_mapi_property_type:
            return "ErrorUnsupportedMapiPropertyType";
        case response_code::error_unsupported_mime_conversion:
            return "ErrorUnsupportedMimeConversion";
        case response_code::error_unsupported_path_for_query:
            return "ErrorUnsupportedPathForQuery";
        case response_code::error_unsupported_path_for_sort_group:
            return "ErrorUnsupportedPathForSortGroup";
        case response_code::error_unsupported_property_definition:
            return "ErrorUnsupportedPropertyDefinition";
        case response_code::error_unsupported_query_filter:
            return "ErrorUnsupportedQueryFilter";
        case response_code::error_unsupported_recurrence:
            return "ErrorUnsupportedRecurrence";
        case response_code::error_unsupported_sub_filter:
            return "ErrorUnsupportedSubFilter";
        case response_code::error_unsupported_type_for_conversion:
            return "ErrorUnsupportedTypeForConversion";
        case response_code::error_update_delegates_failed:
            return "ErrorUpdateDelegatesFailed";
        case response_code::error_update_property_mismatch:
            return "ErrorUpdatePropertyMismatch";
        case response_code::error_user_not_unified_messaging_enabled:
            return "ErrorUserNotUnifiedMessagingEnabled";
        case response_code::error_user_not_allowed_by_policy:
            return "ErrorUserNotAllowedByPolicy";
        case response_code::error_user_without_federated_proxy_address:
            return "ErrorUserWithoutFederatedProxyAddress";
        case response_code::error_value_out_of_range:
            return "ErrorValueOutOfRange";
        case response_code::error_virus_detected:
            return "ErrorVirusDetected";
        case response_code::error_virus_message_deleted:
            return "ErrorVirusMessageDeleted";
        case response_code::error_voice_mail_not_implemented:
            return "ErrorVoiceMailNotImplemented";
        case response_code::error_web_request_in_invalid_state:
            return "ErrorWebRequestInInvalidState";
        case response_code::error_win32_interop_error:
            return "ErrorWin32InteropError";
        case response_code::error_working_hours_save_failed:
            return "ErrorWorkingHoursSaveFailed";
        case response_code::error_working_hours_xml_malformed:
            return "ErrorWorkingHoursXmlMalformed";
        case response_code::error_wrong_server_version:
            return "ErrorWrongServerVersion";
        case response_code::error_wrong_server_version_delegate:
            return "ErrorWrongServerVersionDelegate";
        case response_code::error_missing_information_sharing_folder_id:
            return "ErrorMissingInformationSharingFolderId";
        case response_code::error_duplicate_soap_header:
            return "ErrorDuplicateSOAPHeader";
        case response_code::error_sharing_synchronization_failed:
            return "ErrorSharingSynchronizationFailed";
        case response_code::error_sharing_no_external_ews_available:
            return "ErrorSharingNoExternalEwsAvailable";
        case response_code::error_free_busy_dl_limit_reached:
            return "ErrorFreeBusyDLLimitReached";
        case response_code::error_invalid_get_sharing_folder_request:
            return "ErrorInvalidGetSharingFolderRequest";
        case response_code::error_not_allowed_external_sharing_by_policy:
            return "ErrorNotAllowedExternalSharingByPolicy";
        case response_code::error_permission_not_allowed_by_policy:
            return "ErrorPermissionNotAllowedByPolicy";
        case response_code::error_message_tracking_transient_error:
            return "ErrorMessageTrackingTransientError";
        case response_code::error_apply_conversation_action_failed:
            return "ErrorApplyConversationActionFailed";
        case response_code::error_inbox_rules_validation_error:
            return "ErrorInboxRulesValidationError";
        case response_code::error_outlook_rule_blob_exists:
            return "ErrorOutlookRuleBlobExists";
        case response_code::error_rules_over_quota:
            return "ErrorRulesOverQuota";
        case response_code::error_new_event_stream_connection_opened:
            return "ErrorNewEventStreamConnectionOpened";
        case response_code::error_missed_notification_events:
            return "ErrorMissedNotificationEvents";
        case response_code::error_duplicate_legacy_distinguished_name:
            return "ErrorDuplicateLegacyDistinguishedName";
        case response_code::error_invalid_client_access_token_request:
            return "ErrorInvalidClientAccessTokenRequest";
        case response_code::error_no_speech_detected:
            return "ErrorNoSpeechDetected";
        case response_code::error_um_server_unavailable:
            return "ErrorUMServerUnavailable";
        case response_code::error_recipient_not_found:
            return "ErrorRecipientNotFound";
        case response_code::error_recognizer_not_installed:
            return "ErrorRecognizerNotInstalled";
        case response_code::error_speech_grammar_error:
            return "ErrorSpeechGrammarError";
        case response_code::error_invalid_management_role_header:
            return "ErrorInvalidManagementRoleHeader";
        case response_code::error_location_services_disabled:
            return "ErrorLocationServicesDisabled";
        case response_code::error_location_services_request_timed_out:
            return "ErrorLocationServicesRequestTimedOut";
        case response_code::error_location_services_request_failed:
            return "ErrorLocationServicesRequestFailed";
        case response_code::error_location_services_invalid_request:
            return "ErrorLocationServicesInvalidRequest";
        case response_code::error_weather_service_disabled:
            return "ErrorWeatherServiceDisabled";
        case response_code::
            error_mailbox_scope_not_allowed_without_query_string:
            return "ErrorMailboxScopeNotAllowedWithoutQueryString";
        case response_code::error_archive_mailbox_search_failed:
            return "ErrorArchiveMailboxSearchFailed";
        case response_code::error_get_remote_archive_folder_failed:
            return "ErrorGetRemoteArchiveFolderFailed";
        case response_code::error_find_remote_archive_folder_failed:
            return "ErrorFindRemoteArchiveFolderFailed";
        case response_code::error_get_remote_archive_item_failed:
            return "ErrorGetRemoteArchiveItemFailed";
        case response_code::error_export_remote_archive_items_failed:
            return "ErrorExportRemoteArchiveItemsFailed";
        case response_code::error_invalid_photo_size:
            return "ErrorInvalidPhotoSize";
        case response_code::error_search_query_has_too_many_keywords:
            return "ErrorSearchQueryHasTooManyKeywords";
        case response_code::error_search_too_many_mailboxes:
            return "ErrorSearchTooManyMailboxes";
        case response_code::error_invalid_retention_tag_none:
            return "ErrorInvalidRetentionTagNone";
        case response_code::error_discovery_searches_disabled:
            return "ErrorDiscoverySearchesDisabled";
        case response_code::error_calendar_seek_to_condition_not_supported:
            return "ErrorCalendarSeekToConditionNotSupported";
        case response_code::error_calendar_is_group_mailbox_for_accept:
            return "ErrorCalendarIsGroupMailboxForAccept";
        case response_code::error_calendar_is_group_mailbox_for_decline:
            return "ErrorCalendarIsGroupMailboxForDecline";
        case response_code::error_calendar_is_group_mailbox_for_tentative:
            return "ErrorCalendarIsGroupMailboxForTentative";
        case response_code::
            error_calendar_is_group_mailbox_for_suppress_read_receipt:
            return "ErrorCalendarIsGroupMailboxForSuppressReadReceipt";
        case response_code::error_organization_access_blocked:
            return "ErrorOrganizationAccessBlocked";
        case response_code::error_invalid_license:
            return "ErrorInvalidLicense";
        case response_code::
            error_message_per_folder_count_receive_quota_exceeded:
            return "ErrorMessagePerFolderCountReceiveQuotaExceeded";
        default:
            throw exception("Unrecognized response code");
        }
    }
} // namespace internal

//! \brief Defines the base point for
//! paged searches with <tt>\<FindItem\></tt>
//! and <tt>\<FindFolder\></tt>.
enum class paging_base_point
{
    //! The paged view starts at the beginning of the found conversation or item
    //! set.
    beginning,

    //! The paged view starts at the end of the found conversation or item set.
    end
};

namespace internal
{
    inline std::string enum_to_str(paging_base_point base)
    {
        switch (base)
        {
        case paging_base_point::beginning:
            return "Beginning";
        case paging_base_point::end:
            return "End";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! \brief Represents the unique identifiers of the time zones.
//! These IDs are specific to windows and differ from the ISO
//! time zone names.
enum class time_zone
{
    none,
    dateline_standard_time,
    utc_minus_11,
    aleutian_standard_time,
    hawaiian_standard_time,
    marquesas_standard_time,
    alaskan_standard_time,
    utc_minus_09,
    pacific_standard_time_mexico,
    utc_minus_08,
    pacific_standard_time,
    us_mountain_standard_time,
    mountain_standard_time_mexico,
    mountain_standard_time,
    central_america_standard_time,
    central_standard_time,
    easter_island_standard_time,
    central_standard_time_mexico,
    canada_central_standard_time,
    sa_pacific_standard_time,
    eastern_standard_time_mexico,
    eastern_standard_time,
    haiti_standard_time,
    cuba_standard_time,
    us_eastern_standard_time,
    turks_and_caicos_standard_time,
    paraguay_standard_time,
    atlantic_standard_time,
    venezuela_standard_time,
    central_brazilian_standard_time,
    sa_western_standard_time,
    pacific_sa_standard_time,
    newfoundland_standard_time,
    tocantins_standard_time,
    e_south_america_standard_time,
    sa_eastern_standard_time,
    argentina_standard_time,
    greenland_standard_time,
    montevideo_standard_time,
    magallanes_standard_time,
    saint_pierre_standard_time,
    bahia_standard_time,
    utc_minus_02,
    mid_minus_atlantic_standard_time,
    azores_standard_time,
    cape_verde_standard_time,
    utc,
    morocco_standard_time,
    gmt_standard_time,
    greenwich_standard_time,
    w_europe_standard_time,
    central_europe_standard_time,
    romance_standard_time,
    central_european_standard_time,
    w_central_africa_standard_time,
    jordan_standard_time,
    gtb_standard_time,
    middle_east_standard_time,
    egypt_standard_time,
    e_europe_standard_time,
    syria_standard_time,
    west_bank_standard_time,
    south_africa_standard_time,
    fle_standard_time,
    israel_standard_time,
    kaliningrad_standard_time,
    sudan_standard_time,
    libya_standard_time,
    namibia_standard_time,
    arabic_standard_time,
    turkey_standard_time,
    arab_standard_time,
    belarus_standard_time,
    russian_standard_time,
    e_africa_standard_time,
    iran_standard_time,
    arabian_standard_time,
    astrakhan_standard_time,
    azerbaijan_standard_time,
    russia_time_zone_3,
    mauritius_standard_time,
    saratov_standard_time,
    georgian_standard_time,
    caucasus_standard_time,
    afghanistan_standard_time,
    west_asia_standard_time,
    ekaterinburg_standard_time,
    pakistan_standard_time,
    india_standard_time,
    sri_lanka_standard_time,
    nepal_standard_time,
    central_asia_standard_time,
    bangladesh_standard_time,
    omsk_standard_time,
    myanmar_standard_time,
    se_asia_standard_time,
    altai_standard_time,
    w_mongolia_standard_time,
    north_asia_standard_time,
    n_central_asia_standard_time,
    tomsk_standard_time,
    china_standard_time,
    north_asia_east_standard_time,
    singapore_standard_time,
    w_australia_standard_time,
    taipei_standard_time,
    ulaanbaatar_standard_time,
    north_korea_standard_time,
    aus_central_w_standard_time,
    transbaikal_standard_time,
    tokyo_standard_time,
    korea_standard_time,
    yakutsk_standard_time,
    cen_australia_standard_time,
    aus_central_standard_time,
    e_australia_standard_time,
    aus_eastern_standard_time,
    west_pacific_standard_time,
    tasmania_standard_time,
    vladivostok_standard_time,
    lord_howe_standard_time,
    bougainville_standard_time,
    russia_time_zone_10,
    magadan_standard_time,
    norfolk_standard_time,
    sakhalin_standard_time,
    central_pacific_standard_time,
    russia_time_zone_11,
    new_zealand_standard_time,
    utc_plus_12,
    fiji_standard_time,
    kamchatka_standard_time,
    chatham_islands_standard_time,
    utc_plus_13,
    tonga_standard_time,
    samoa_standard_time,
    line_islands_standard_time,
};

namespace internal
{
    inline std::string enum_to_str(time_zone tz)
    {
        switch (tz)
        {
        case time_zone::dateline_standard_time:
            return "Dateline Standard Time";
        case time_zone::utc_minus_11:
            return "UTC-11";
        case time_zone::aleutian_standard_time:
            return "Aleutian Standard Time";
        case time_zone::hawaiian_standard_time:
            return "Hawaiian Standard Time";
        case time_zone::marquesas_standard_time:
            return "Marquesas Standard Time";
        case time_zone::alaskan_standard_time:
            return "Alaskan Standard Time";
        case time_zone::utc_minus_09:
            return "UTC-09";
        case time_zone::pacific_standard_time_mexico:
            return "Pacific Standard Time (Mexico)";
        case time_zone::utc_minus_08:
            return "UTC-08";
        case time_zone::pacific_standard_time:
            return "Pacific Standard Time";
        case time_zone::us_mountain_standard_time:
            return "US Mountain Standard Time";
        case time_zone::mountain_standard_time_mexico:
            return "Mountain Standard Time (Mexico)";
        case time_zone::mountain_standard_time:
            return "Mountain Standard Time";
        case time_zone::central_america_standard_time:
            return "Central America Standard Time";
        case time_zone::central_standard_time:
            return "Central Standard Time";
        case time_zone::easter_island_standard_time:
            return "Easter Island Standard Time";
        case time_zone::central_standard_time_mexico:
            return "Central Standard Time (Mexico)";
        case time_zone::canada_central_standard_time:
            return "Canada Central Standard Time";
        case time_zone::sa_pacific_standard_time:
            return "SA Pacific Standard Time";
        case time_zone::eastern_standard_time_mexico:
            return "Eastern Standard Time (Mexico)";
        case time_zone::eastern_standard_time:
            return "Eastern Standard Time";
        case time_zone::haiti_standard_time:
            return "Haiti Standard Time";
        case time_zone::cuba_standard_time:
            return "Cuba Standard Time";
        case time_zone::us_eastern_standard_time:
            return "US Eastern Standard Time";
        case time_zone::turks_and_caicos_standard_time:
            return "Turks And Caicos Standard Time";
        case time_zone::paraguay_standard_time:
            return "Paraguay Standard Time";
        case time_zone::atlantic_standard_time:
            return "Atlantic Standard Time";
        case time_zone::venezuela_standard_time:
            return "Venezuela Standard Time";
        case time_zone::central_brazilian_standard_time:
            return "Central Brazilian Standard Time";
        case time_zone::sa_western_standard_time:
            return "SA Western Standard Time";
        case time_zone::pacific_sa_standard_time:
            return "Pacific SA Standard Time";
        case time_zone::newfoundland_standard_time:
            return "Newfoundland Standard Time";
        case time_zone::tocantins_standard_time:
            return "Tocantins Standard Time";
        case time_zone::e_south_america_standard_time:
            return "E. South America Standard Time";
        case time_zone::sa_eastern_standard_time:
            return "SA Eastern Standard Time";
        case time_zone::argentina_standard_time:
            return "Argentina Standard Time";
        case time_zone::greenland_standard_time:
            return "Greenland Standard Time";
        case time_zone::montevideo_standard_time:
            return "Montevideo Standard Time";
        case time_zone::magallanes_standard_time:
            return "Magallanes Standard Time";
        case time_zone::saint_pierre_standard_time:
            return "Saint Pierre Standard Time";
        case time_zone::bahia_standard_time:
            return "Bahia Standard Time";
        case time_zone::utc_minus_02:
            return "UTC-02";
        case time_zone::mid_minus_atlantic_standard_time:
            return "Mid-Atlantic Standard Time";
        case time_zone::azores_standard_time:
            return "Azores Standard Time";
        case time_zone::cape_verde_standard_time:
            return "Cape Verde Standard Time";
        case time_zone::utc:
            return "UTC";
        case time_zone::morocco_standard_time:
            return "Morocco Standard Time";
        case time_zone::gmt_standard_time:
            return "GMT Standard Time";
        case time_zone::greenwich_standard_time:
            return "Greenwich Standard Time";
        case time_zone::w_europe_standard_time:
            return "W. Europe Standard Time";
        case time_zone::central_europe_standard_time:
            return "Central Europe Standard Time";
        case time_zone::romance_standard_time:
            return "Romance Standard Time";
        case time_zone::central_european_standard_time:
            return "Central European Standard Time";
        case time_zone::w_central_africa_standard_time:
            return "W. Central Africa Standard Time";
        case time_zone::jordan_standard_time:
            return "Jordan Standard Time";
        case time_zone::gtb_standard_time:
            return "GTB Standard Time";
        case time_zone::middle_east_standard_time:
            return "Middle East Standard Time";
        case time_zone::egypt_standard_time:
            return "Egypt Standard Time";
        case time_zone::e_europe_standard_time:
            return "E. Europe Standard Time";
        case time_zone::syria_standard_time:
            return "Syria Standard Time";
        case time_zone::west_bank_standard_time:
            return "West Bank Standard Time";
        case time_zone::south_africa_standard_time:
            return "South Africa Standard Time";
        case time_zone::fle_standard_time:
            return "FLE Standard Time";
        case time_zone::israel_standard_time:
            return "Israel Standard Time";
        case time_zone::kaliningrad_standard_time:
            return "Kaliningrad Standard Time";
        case time_zone::sudan_standard_time:
            return "Sudan Standard Time";
        case time_zone::libya_standard_time:
            return "Libya Standard Time";
        case time_zone::namibia_standard_time:
            return "Namibia Standard Time";
        case time_zone::arabic_standard_time:
            return "Arabic Standard Time";
        case time_zone::turkey_standard_time:
            return "Turkey Standard Time";
        case time_zone::arab_standard_time:
            return "Arab Standard Time";
        case time_zone::belarus_standard_time:
            return "Belarus Standard Time";
        case time_zone::russian_standard_time:
            return "Russian Standard Time";
        case time_zone::e_africa_standard_time:
            return "E. Africa Standard Time";
        case time_zone::iran_standard_time:
            return "Iran Standard Time";
        case time_zone::arabian_standard_time:
            return "Arabian Standard Time";
        case time_zone::astrakhan_standard_time:
            return "Astrakhan Standard Time";
        case time_zone::azerbaijan_standard_time:
            return "Azerbaijan Standard Time";
        case time_zone::russia_time_zone_3:
            return "Russia Time Zone 3";
        case time_zone::mauritius_standard_time:
            return "Mauritius Standard Time";
        case time_zone::saratov_standard_time:
            return "Saratov Standard Time";
        case time_zone::georgian_standard_time:
            return "Georgian Standard Time";
        case time_zone::caucasus_standard_time:
            return "Caucasus Standard Time";
        case time_zone::afghanistan_standard_time:
            return "Afghanistan Standard Time";
        case time_zone::west_asia_standard_time:
            return "West Asia Standard Time";
        case time_zone::ekaterinburg_standard_time:
            return "Ekaterinburg Standard Time";
        case time_zone::pakistan_standard_time:
            return "Pakistan Standard Time";
        case time_zone::india_standard_time:
            return "India Standard Time";
        case time_zone::sri_lanka_standard_time:
            return "Sri Lanka Standard Time";
        case time_zone::nepal_standard_time:
            return "Nepal Standard Time";
        case time_zone::central_asia_standard_time:
            return "Central Asia Standard Time";
        case time_zone::bangladesh_standard_time:
            return "Bangladesh Standard Time";
        case time_zone::omsk_standard_time:
            return "Omsk Standard Time";
        case time_zone::myanmar_standard_time:
            return "Myanmar Standard Time";
        case time_zone::se_asia_standard_time:
            return "SE Asia Standard Time";
        case time_zone::altai_standard_time:
            return "Altai Standard Time";
        case time_zone::w_mongolia_standard_time:
            return "W. Mongolia Standard Time";
        case time_zone::north_asia_standard_time:
            return "North Asia Standard Time";
        case time_zone::n_central_asia_standard_time:
            return "N. Central Asia Standard Time";
        case time_zone::tomsk_standard_time:
            return "Tomsk Standard Time";
        case time_zone::china_standard_time:
            return "China Standard Time";
        case time_zone::north_asia_east_standard_time:
            return "North Asia East Standard Time";
        case time_zone::singapore_standard_time:
            return "Singapore Standard Time";
        case time_zone::w_australia_standard_time:
            return "W. Australia Standard Time";
        case time_zone::taipei_standard_time:
            return "Taipei Standard Time";
        case time_zone::ulaanbaatar_standard_time:
            return "Ulaanbaatar Standard Time";
        case time_zone::north_korea_standard_time:
            return "North Korea Standard Time";
        case time_zone::aus_central_w_standard_time:
            return "Aus Central W. Standard Time";
        case time_zone::transbaikal_standard_time:
            return "Transbaikal Standard Time";
        case time_zone::tokyo_standard_time:
            return "Tokyo Standard Time";
        case time_zone::korea_standard_time:
            return "Korea Standard Time";
        case time_zone::yakutsk_standard_time:
            return "Yakutsk Standard Time";
        case time_zone::cen_australia_standard_time:
            return "Cen. Australia Standard Time";
        case time_zone::aus_central_standard_time:
            return "AUS Central Standard Time";
        case time_zone::e_australia_standard_time:
            return "E. Australia Standard Time";
        case time_zone::aus_eastern_standard_time:
            return "AUS Eastern Standard Time";
        case time_zone::west_pacific_standard_time:
            return "West Pacific Standard Time";
        case time_zone::tasmania_standard_time:
            return "Tasmania Standard Time";
        case time_zone::vladivostok_standard_time:
            return "Vladivostok Standard Time";
        case time_zone::lord_howe_standard_time:
            return "Lord Howe Standard Time";
        case time_zone::bougainville_standard_time:
            return "Bougainville Standard Time";
        case time_zone::russia_time_zone_10:
            return "Russia Time Zone 10";
        case time_zone::magadan_standard_time:
            return "Magadan Standard Time";
        case time_zone::norfolk_standard_time:
            return "Norfolk Standard Time";
        case time_zone::sakhalin_standard_time:
            return "Sakhalin Standard Time";
        case time_zone::central_pacific_standard_time:
            return "Central Pacific Standard Time";
        case time_zone::russia_time_zone_11:
            return "Russia Time Zone 11";
        case time_zone::new_zealand_standard_time:
            return "New Zealand Standard Time";
        case time_zone::utc_plus_12:
            return "UTC+12";
        case time_zone::fiji_standard_time:
            return "Fiji Standard Time";
        case time_zone::kamchatka_standard_time:
            return "Kamchatka Standard Time";
        case time_zone::chatham_islands_standard_time:
            return "Chatham Islands Standard Time";
        case time_zone::utc_plus_13:
            return "UTC+13";
        case time_zone::tonga_standard_time:
            return "Tonga Standard Time";
        case time_zone::samoa_standard_time:
            return "Samoa Standard Time";
        case time_zone::line_islands_standard_time:
            return "Line Islands Standard Time";
        default:
            throw exception("Bad enum value");
        }
    }

    inline time_zone str_to_time_zone(const std::string& str)
    {
        if (str == "Dateline Standard Time")
        {
            return time_zone::dateline_standard_time;
        }
        if (str == "UTC-11")
        {
            return time_zone::utc_minus_11;
        }
        if (str == "Aleutian Standard Time")
        {
            return time_zone::aleutian_standard_time;
        }
        if (str == "Hawaiian Standard Time")
        {
            return time_zone::hawaiian_standard_time;
        }
        if (str == "Marquesas Standard Time")
        {
            return time_zone::marquesas_standard_time;
        }
        if (str == "Alaskan Standard Time")
        {
            return time_zone::alaskan_standard_time;
        }
        if (str == "UTC-09")
        {
            return time_zone::utc_minus_09;
        }
        if (str == "Pacific Standard Time (Mexico)")
        {
            return time_zone::pacific_standard_time_mexico;
        }
        if (str == "UTC-08")
        {
            return time_zone::utc_minus_08;
        }
        if (str == "Pacific Standard Time")
        {
            return time_zone::pacific_standard_time;
        }
        if (str == "US Mountain Standard Time")
        {
            return time_zone::us_mountain_standard_time;
        }
        if (str == "Mountain Standard Time (Mexico)")
        {
            return time_zone::mountain_standard_time_mexico;
        }
        if (str == "Mountain Standard Time")
        {
            return time_zone::mountain_standard_time;
        }
        if (str == "Central America Standard Time")
        {
            return time_zone::central_america_standard_time;
        }
        if (str == "Central Standard Time")
        {
            return time_zone::central_standard_time;
        }
        if (str == "Easter Island Standard Time")
        {
            return time_zone::easter_island_standard_time;
        }
        if (str == "Central Standard Time (Mexico)")
        {
            return time_zone::central_standard_time_mexico;
        }
        if (str == "Canada Central Standard Time")
        {
            return time_zone::canada_central_standard_time;
        }
        if (str == "SA Pacific Standard Time")
        {
            return time_zone::sa_pacific_standard_time;
        }
        if (str == "Eastern Standard Time (Mexico)")
        {
            return time_zone::eastern_standard_time_mexico;
        }
        if (str == "Eastern Standard Time")
        {
            return time_zone::eastern_standard_time;
        }
        if (str == "Haiti Standard Time")
        {
            return time_zone::haiti_standard_time;
        }
        if (str == "Cuba Standard Time")
        {
            return time_zone::cuba_standard_time;
        }
        if (str == "US Eastern Standard Time")
        {
            return time_zone::us_eastern_standard_time;
        }
        if (str == "Turks And Caicos Standard Time")
        {
            return time_zone::turks_and_caicos_standard_time;
        }
        if (str == "Paraguay Standard Time")
        {
            return time_zone::paraguay_standard_time;
        }
        if (str == "Atlantic Standard Time")
        {
            return time_zone::atlantic_standard_time;
        }
        if (str == "Venezuela Standard Time")
        {
            return time_zone::venezuela_standard_time;
        }
        if (str == "Central Brazilian Standard Time")
        {
            return time_zone::central_brazilian_standard_time;
        }
        if (str == "SA Western Standard Time")
        {
            return time_zone::sa_western_standard_time;
        }
        if (str == "Pacific SA Standard Time")
        {
            return time_zone::pacific_sa_standard_time;
        }
        if (str == "Newfoundland Standard Time")
        {
            return time_zone::newfoundland_standard_time;
        }
        if (str == "Tocantins Standard Time")
        {
            return time_zone::tocantins_standard_time;
        }
        if (str == "E. South America Standard Time")
        {
            return time_zone::e_south_america_standard_time;
        }
        if (str == "SA Eastern Standard Time")
        {
            return time_zone::sa_eastern_standard_time;
        }
        if (str == "Argentina Standard Time")
        {
            return time_zone::argentina_standard_time;
        }
        if (str == "Greenland Standard Time")
        {
            return time_zone::greenland_standard_time;
        }
        if (str == "Montevideo Standard Time")
        {
            return time_zone::montevideo_standard_time;
        }
        if (str == "Magallanes Standard Time")
        {
            return time_zone::magallanes_standard_time;
        }
        if (str == "Saint Pierre Standard Time")
        {
            return time_zone::saint_pierre_standard_time;
        }
        if (str == "Bahia Standard Time")
        {
            return time_zone::bahia_standard_time;
        }
        if (str == "UTC-02")
        {
            return time_zone::utc_minus_02;
        }
        if (str == "Mid-Atlantic Standard Time")
        {
            return time_zone::mid_minus_atlantic_standard_time;
        }
        if (str == "Azores Standard Time")
        {
            return time_zone::azores_standard_time;
        }
        if (str == "Cape Verde Standard Time")
        {
            return time_zone::cape_verde_standard_time;
        }
        if (str == "UTC")
        {
            return time_zone::utc;
        }
        if (str == "Morocco Standard Time")
        {
            return time_zone::morocco_standard_time;
        }
        if (str == "GMT Standard Time")
        {
            return time_zone::gmt_standard_time;
        }
        if (str == "Greenwich Standard Time")
        {
            return time_zone::greenwich_standard_time;
        }
        if (str == "W. Europe Standard Time")
        {
            return time_zone::w_europe_standard_time;
        }
        if (str == "Central Europe Standard Time")
        {
            return time_zone::central_europe_standard_time;
        }
        if (str == "Romance Standard Time")
        {
            return time_zone::romance_standard_time;
        }
        if (str == "Central European Standard Time")
        {
            return time_zone::central_european_standard_time;
        }
        if (str == "W. Central Africa Standard Time")
        {
            return time_zone::w_central_africa_standard_time;
        }
        if (str == "Jordan Standard Time")
        {
            return time_zone::jordan_standard_time;
        }
        if (str == "GTB Standard Time")
        {
            return time_zone::gtb_standard_time;
        }
        if (str == "Middle East Standard Time")
        {
            return time_zone::middle_east_standard_time;
        }
        if (str == "Egypt Standard Time")
        {
            return time_zone::egypt_standard_time;
        }
        if (str == "E. Europe Standard Time")
        {
            return time_zone::e_europe_standard_time;
        }
        if (str == "Syria Standard Time")
        {
            return time_zone::syria_standard_time;
        }
        if (str == "West Bank Standard Time")
        {
            return time_zone::west_bank_standard_time;
        }
        if (str == "South Africa Standard Time")
        {
            return time_zone::south_africa_standard_time;
        }
        if (str == "FLE Standard Time")
        {
            return time_zone::fle_standard_time;
        }
        if (str == "Israel Standard Time")
        {
            return time_zone::israel_standard_time;
        }
        if (str == "Kaliningrad Standard Time")
        {
            return time_zone::kaliningrad_standard_time;
        }
        if (str == "Sudan Standard Time")
        {
            return time_zone::sudan_standard_time;
        }
        if (str == "Libya Standard Time")
        {
            return time_zone::libya_standard_time;
        }
        if (str == "Namibia Standard Time")
        {
            return time_zone::namibia_standard_time;
        }
        if (str == "Arabic Standard Time")
        {
            return time_zone::arabic_standard_time;
        }
        if (str == "Turkey Standard Time")
        {
            return time_zone::turkey_standard_time;
        }
        if (str == "Arab Standard Time")
        {
            return time_zone::arab_standard_time;
        }
        if (str == "Belarus Standard Time")
        {
            return time_zone::belarus_standard_time;
        }
        if (str == "Russian Standard Time")
        {
            return time_zone::russian_standard_time;
        }
        if (str == "E. Africa Standard Time")
        {
            return time_zone::e_africa_standard_time;
        }
        if (str == "Iran Standard Time")
        {
            return time_zone::iran_standard_time;
        }
        if (str == "Arabian Standard Time")
        {
            return time_zone::arabian_standard_time;
        }
        if (str == "Astrakhan Standard Time")
        {
            return time_zone::astrakhan_standard_time;
        }
        if (str == "Azerbaijan Standard Time")
        {
            return time_zone::azerbaijan_standard_time;
        }
        if (str == "Russia Time Zone 3")
        {
            return time_zone::russia_time_zone_3;
        }
        if (str == "Mauritius Standard Time")
        {
            return time_zone::mauritius_standard_time;
        }
        if (str == "Saratov Standard Time")
        {
            return time_zone::saratov_standard_time;
        }
        if (str == "Georgian Standard Time")
        {
            return time_zone::georgian_standard_time;
        }
        if (str == "Caucasus Standard Time")
        {
            return time_zone::caucasus_standard_time;
        }
        if (str == "Afghanistan Standard Time")
        {
            return time_zone::afghanistan_standard_time;
        }
        if (str == "West Asia Standard Time")
        {
            return time_zone::west_asia_standard_time;
        }
        if (str == "Ekaterinburg Standard Time")
        {
            return time_zone::ekaterinburg_standard_time;
        }
        if (str == "Pakistan Standard Time")
        {
            return time_zone::pakistan_standard_time;
        }
        if (str == "India Standard Time")
        {
            return time_zone::india_standard_time;
        }
        if (str == "Sri Lanka Standard Time")
        {
            return time_zone::sri_lanka_standard_time;
        }
        if (str == "Nepal Standard Time")
        {
            return time_zone::nepal_standard_time;
        }
        if (str == "Central Asia Standard Time")
        {
            return time_zone::central_asia_standard_time;
        }
        if (str == "Bangladesh Standard Time")
        {
            return time_zone::bangladesh_standard_time;
        }
        if (str == "Omsk Standard Time")
        {
            return time_zone::omsk_standard_time;
        }
        if (str == "Myanmar Standard Time")
        {
            return time_zone::myanmar_standard_time;
        }
        if (str == "SE Asia Standard Time")
        {
            return time_zone::se_asia_standard_time;
        }
        if (str == "Altai Standard Time")
        {
            return time_zone::altai_standard_time;
        }
        if (str == "W. Mongolia Standard Time")
        {
            return time_zone::w_mongolia_standard_time;
        }
        if (str == "North Asia Standard Time")
        {
            return time_zone::north_asia_standard_time;
        }
        if (str == "N. Central Asia Standard Time")
        {
            return time_zone::n_central_asia_standard_time;
        }
        if (str == "Tomsk Standard Time")
        {
            return time_zone::tomsk_standard_time;
        }
        if (str == "China Standard Time")
        {
            return time_zone::china_standard_time;
        }
        if (str == "North Asia East Standard Time")
        {
            return time_zone::north_asia_east_standard_time;
        }
        if (str == "Singapore Standard Time")
        {
            return time_zone::singapore_standard_time;
        }
        if (str == "W. Australia Standard Time")
        {
            return time_zone::w_australia_standard_time;
        }
        if (str == "Taipei Standard Time")
        {
            return time_zone::taipei_standard_time;
        }
        if (str == "Ulaanbaatar Standard Time")
        {
            return time_zone::ulaanbaatar_standard_time;
        }
        if (str == "North Korea Standard Time")
        {
            return time_zone::north_korea_standard_time;
        }
        if (str == "Aus Central W. Standard Time")
        {
            return time_zone::aus_central_w_standard_time;
        }
        if (str == "Transbaikal Standard Time")
        {
            return time_zone::transbaikal_standard_time;
        }
        if (str == "Tokyo Standard Time")
        {
            return time_zone::tokyo_standard_time;
        }
        if (str == "Korea Standard Time")
        {
            return time_zone::korea_standard_time;
        }
        if (str == "Yakutsk Standard Time")
        {
            return time_zone::yakutsk_standard_time;
        }
        if (str == "Cen. Australia Standard Time")
        {
            return time_zone::cen_australia_standard_time;
        }
        if (str == "AUS Central Standard Time")
        {
            return time_zone::aus_central_standard_time;
        }
        if (str == "E. Australia Standard Time")
        {
            return time_zone::e_australia_standard_time;
        }
        if (str == "AUS Eastern Standard Time")
        {
            return time_zone::aus_eastern_standard_time;
        }
        if (str == "West Pacific Standard Time")
        {
            return time_zone::west_pacific_standard_time;
        }
        if (str == "Tasmania Standard Time")
        {
            return time_zone::tasmania_standard_time;
        }
        if (str == "Vladivostok Standard Time")
        {
            return time_zone::vladivostok_standard_time;
        }
        if (str == "Lord Howe Standard Time")
        {
            return time_zone::lord_howe_standard_time;
        }
        if (str == "Bougainville Standard Time")
        {
            return time_zone::bougainville_standard_time;
        }
        if (str == "Russia Time Zone 10")
        {
            return time_zone::russia_time_zone_10;
        }
        if (str == "Magadan Standard Time")
        {
            return time_zone::magadan_standard_time;
        }
        if (str == "Norfolk Standard Time")
        {
            return time_zone::norfolk_standard_time;
        }
        if (str == "Sakhalin Standard Time")
        {
            return time_zone::sakhalin_standard_time;
        }
        if (str == "Central Pacific Standard Time")
        {
            return time_zone::central_pacific_standard_time;
        }
        if (str == "Russia Time Zone 11")
        {
            return time_zone::russia_time_zone_11;
        }
        if (str == "New Zealand Standard Time")
        {
            return time_zone::new_zealand_standard_time;
        }
        if (str == "UTC+12")
        {
            return time_zone::utc_plus_12;
        }
        if (str == "Fiji Standard Time")
        {
            return time_zone::fiji_standard_time;
        }
        if (str == "Kamchatka Standard Time")
        {
            return time_zone::kamchatka_standard_time;
        }
        if (str == "Chatham Islands Standard Time")
        {
            return time_zone::chatham_islands_standard_time;
        }
        if (str == "UTC+13")
        {
            return time_zone::utc_plus_13;
        }
        if (str == "Tonga Standard Time")
        {
            return time_zone::tonga_standard_time;
        }
        if (str == "Samoa Standard Time")
        {
            return time_zone::samoa_standard_time;
        }
        if (str == "Line Islands Standard Time")
        {
            return time_zone::line_islands_standard_time;
        }

        throw exception("Unrecognized <TimeZone>");
    }
} // namespace internal

//! \brief Defines the different values for the
//! <tt>\<RequestServerVersion\></tt> element.
enum class server_version
{
    //! Target the schema files for the initial release version of
    //! Exchange 2007
    exchange_2007,

    //! Target the schema files for Exchange 2007 Service Pack 1 (SP1),
    //! Exchange 2007 Service Pack 2 (SP2), and
    //! Exchange 2007 Service Pack 3 (SP3)
    exchange_2007_sp1,

    //! Target the schema files for Exchange 2010
    exchange_2010,

    //! Target the schema files for Exchange 2010 Service Pack 1 (SP1)
    exchange_2010_sp1,

    //! Target the schema files for Exchange 2010 Service Pack 2 (SP2)
    //! and Exchange 2010 Service Pack 3 (SP3)
    exchange_2010_sp2,

    //! Target the schema files for Exchange 2013
    exchange_2013,

    //! Target the schema files for Exchange 2013 Service Pack 1 (SP1)
    exchange_2013_sp1
};

namespace internal
{
    inline std::string enum_to_str(server_version vers)
    {
        switch (vers)
        {
        case server_version::exchange_2007:
            return "Exchange2007";
        case server_version::exchange_2007_sp1:
            return "Exchange2007_SP1";
        case server_version::exchange_2010:
            return "Exchange2010";
        case server_version::exchange_2010_sp1:
            return "Exchange2010_SP1";
        case server_version::exchange_2010_sp2:
            return "Exchange2010_SP2";
        case server_version::exchange_2013:
            return "Exchange2013";
        case server_version::exchange_2013_sp1:
            return "Exchange2013_SP1";
        default:
            throw exception("Bad enum value");
        }
    }

    inline server_version str_to_server_version(const std::string& str)
    {
        if (str == "Exchange2007")
        {
            return server_version::exchange_2007;
        }
        else if (str == "Exchange2007_SP1")
        {
            return server_version::exchange_2007_sp1;
        }
        else if (str == "Exchange2010")
        {
            return server_version::exchange_2010;
        }
        else if (str == "Exchange2010_SP1")
        {
            return server_version::exchange_2010_sp1;
        }
        else if (str == "Exchange2010_SP2")
        {
            return server_version::exchange_2010_sp2;
        }
        else if (str == "Exchange2013")
        {
            return server_version::exchange_2013;
        }
        else if (str == "Exchange2013_SP1")
        {
            return server_version::exchange_2013_sp1;
        }
        else
        {
            throw exception("Unrecognized <RequestServerVersion>");
        }
    }
} // namespace internal

//! \brief Specifies the set of properties that a GetItem or GetFolder
//! method call will return.
enum class base_shape
{
    //! Return only the item or folder ID
    id_only,

    //! Return the default set of properties
    default_shape,

    //! \brief Return (nearly) all properties.
    //!
    //! Note that some properties still need to be explicitly requested as
    //! additional properties.
    all_properties
};

namespace internal
{
    inline std::string enum_to_str(base_shape shape)
    {
        switch (shape)
        {
        case base_shape::id_only:
            return "IdOnly";
        case base_shape::default_shape:
            return "Default";
        case base_shape::all_properties:
            return "AllProperties";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! \brief Describes how items are deleted from the Exchange store.
//!
//! Side note: we do not provide SoftDelete because that does not make much
//! sense from an EWS perspective.
enum class delete_type
{
    //! The item is removed immediately from the user's mailbox
    hard_delete,

    //! The item is moved to a dedicated "Trash" folder
    move_to_deleted_items
};

namespace internal
{
    inline std::string enum_to_str(delete_type d)
    {
        switch (d)
        {
        case delete_type::hard_delete:
            return "HardDelete";
        case delete_type::move_to_deleted_items:
            return "MoveToDeletedItems";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! Indicates which occurrences of a recurring series should be deleted
enum class affected_task_occurrences
{
    //! Apply an operation to all occurrences in the series
    all_occurrences,

    //! Apply an operation only to the specified occurrence
    specified_occurrence_only
};

namespace internal
{
    inline std::string enum_to_str(affected_task_occurrences val)
    {
        switch (val)
        {
        case affected_task_occurrences::all_occurrences:
            return "AllOccurrences";
        case affected_task_occurrences::specified_occurrence_only:
            return "SpecifiedOccurrenceOnly";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! Describes how attendees will be updated when a meeting changes
enum class send_meeting_invitations_or_cancellations
{
    //! The calendar item is updated but updates are not sent to attendee
    send_to_none,

    //! The calendar item is updated and the meeting update is sent to all
    //! attendees but is not saved in the Sent Items folder
    send_only_to_all,

    //! The calendar item is updated and the meeting update is sent only to
    //! attendees that are affected by the change in the meeting
    send_only_to_changed,

    //! The calendar item is updated, the meeting update is sent to all
    //! attendees, and a copy is saved in the Sent Items folder
    send_to_all_and_save_copy,

    //! The calendar item is updated, the meeting update is sent to all
    //! attendees that are affected by the change in the meeting, and a copy is
    //! saved in the Sent Items folder
    send_to_changed_and_save_copy
};

namespace internal
{
    inline std::string
    enum_to_str(send_meeting_invitations_or_cancellations val)
    {
        switch (val)
        {
        case send_meeting_invitations_or_cancellations::send_to_none:
            return "SendToNone";
        case send_meeting_invitations_or_cancellations::send_only_to_all:
            return "SendOnlyToAll";
        case send_meeting_invitations_or_cancellations::send_only_to_changed:
            return "SendOnlyToChanged";
        case send_meeting_invitations_or_cancellations::
            send_to_all_and_save_copy:
            return "SendToAllAndSaveCopy";
        case send_meeting_invitations_or_cancellations::
            send_to_changed_and_save_copy:
            return "SendToChangedAndSaveCopy";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! Describes how a meeting will be canceled
enum class send_meeting_cancellations
{
    //! The calendar item is deleted without sending a cancellation message
    send_to_none,

    //! The calendar item is deleted and a cancellation message is sent to
    //! all attendees
    send_only_to_all,

    //! The calendar item is deleted and a cancellation message is sent to
    //! all attendees. A copy of the cancellation message is saved in the
    //! Sent Items folder
    send_to_all_and_save_copy
};

typedef send_meeting_cancellations send_meeting_invitations;

namespace internal
{
    inline std::string enum_to_str(send_meeting_cancellations val)
    {
        switch (val)
        {
        case send_meeting_cancellations::send_to_none:
            return "SendToNone";
        case send_meeting_cancellations::send_only_to_all:
            return "SendOnlyToAll";
        case send_meeting_cancellations::send_to_all_and_save_copy:
            return "SendToAllAndSaveCopy";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! \brief The type of conflict resolution to try during an UpdateItem
//! method call.
//!
//! The default value is AutoResolve.
enum class conflict_resolution
{
    //! If there is a conflict, the update operation fails and an error is
    //! returned. The call to update_item never overwrites data that has
    //! changed underneath you!
    never_overwrite,

    //! The update operation automatically resolves any conflict (if it can,
    //! otherwise the request fails)
    auto_resolve,

    //! If there is a conflict, the update operation will overwrite
    //! information. Ignores changes that occurred underneath you; last
    //! writer wins!
    always_overwrite
};

namespace internal
{
    inline std::string enum_to_str(conflict_resolution val)
    {
        switch (val)
        {
        case conflict_resolution::never_overwrite:
            return "NeverOverwrite";
        case conflict_resolution::auto_resolve:
            return "AutoResolve";
        case conflict_resolution::always_overwrite:
            return "AlwaysOverwrite";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! \brief <tt>\<CreateItem\></tt> and <tt>\<UpdateItem\></tt> methods use
//! this attribute. Only
//! applicable to email messages
enum class message_disposition
{
    //! Save the message in a specified folder or in the Drafts folder if
    //! none is given
    save_only,

    //! Send the message and do not save a copy in the sender's mailbox
    send_only,

    //! Send the message and save a copy in a specified folder or in the
    //! mailbox owner's Sent Items folder if none is given
    send_and_save_copy
};

namespace internal
{
    inline std::string enum_to_str(message_disposition val)
    {
        switch (val)
        {
        case message_disposition::save_only:
            return "SaveOnly";
        case message_disposition::send_only:
            return "SendOnly";
        case message_disposition::send_and_save_copy:
            return "SendAndSaveCopy";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! Gets the free/busy status that is associated with the event
enum class free_busy_status
{
    //! The time slot is open for other events
    free,

    //! The time slot is potentially filled
    tentative,

    //! The time slot is filled and attempts by others to schedule a
    //! meeting for this time period should be avoided by an application
    busy,

    //! The user is out-of-office and may not be able to respond to
    //! meeting invitations for new events that occur in the time slot
    out_of_office,

    //! Status is undetermined. You should not explicitly set this.
    //! However the Exchange store might return this value
    no_data,

    //! The time slot associated with the appointment appears as
    //! working elsewhere. The WorkingElsewhere field is applicable for clients
    //! that target Exchange Online and versions of Exchange starting with
    //! Exchange Server 2013.
    working_elsewhere
};

namespace internal
{
    inline std::string enum_to_str(free_busy_status val)
    {
        switch (val)
        {
        case free_busy_status::free:
            return "Free";
        case free_busy_status::tentative:
            return "Tentative";
        case free_busy_status::busy:
            return "Busy";
        case free_busy_status::out_of_office:
            return "OOF";
        case free_busy_status::no_data:
            return "NoData";
        case free_busy_status::working_elsewhere:
            return "WorkingElsewhere";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! The CalendarItemType class represents an Exchange calendar item.
enum class calendar_item_type
{
    //! The item is not associated with a recurring calendar item; default
    //! for new calendar items
    single,

    //! The item is an occurrence of a recurring calendar item
    occurrence,

    //! The item is an exception to a recurring calendar item
    exception,

    //! The item is master for a set of recurring calendar items
    recurring_master
};

//! \brief The ResponseType element represents the type of recipient
//! response that
//! is received for a meeting.
enum class response_type
{
    //! The response type is unknown.
    unknown,
    //! Indicates an organizer response type.
    organizer,
    //! Meeting is tentative accepted.
    tentative,
    //! Meeting is accepted.
    accept,
    //! Meeting is declined.
    decline,
    //! Indicates that no response is received.
    no_response_received
};

namespace internal
{
    inline std::string enum_to_str(response_type val)
    {
        switch (val)
        {
        case response_type::unknown:
            return "Unknown";
        case response_type::organizer:
            return "Organizer";
        case response_type::tentative:
            return "Tentative";
        case response_type::accept:
            return "Accept";
        case response_type::decline:
            return "Decline";
        case response_type::no_response_received:
            return "NoResponseReceived";
        default:
            throw exception("Bad enum value");
        }
    }

    inline response_type str_to_response_type(const std::string& str)
    {
        if (str == "Unknown")
        {
            return response_type::unknown;
        }
        else if (str == "Organizer")
        {
            return response_type::organizer;
        }
        else if (str == "Tentative")
        {
            return response_type::tentative;
        }
        else if (str == "Accept")
        {
            return response_type::accept;
        }
        else if (str == "Decline")
        {
            return response_type::decline;
        }
        else if (str == "NoResponseReceived")
        {
            return response_type::no_response_received;
        }
        else
        {
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! \brief Well known folder names enumeration. Usually rendered to XML as
//! <tt>\<DistinguishedFolderId></tt> element.
enum class standard_folder
{
    //! The Calendar folder
    calendar,

    //! The Contacts folder
    contacts,

    //! The Deleted Items folder
    deleted_items,

    //! The Drafts folder
    drafts,

    //! The Inbox folder
    inbox,

    //! The Journal folder
    journal,

    //! The Notes folder
    notes,

    //! The Outbox folder
    outbox,

    //! The Sent Items folder
    sent_items,

    //! The Tasks folder
    tasks,

    //! The root of the message folder hierarchy
    msg_folder_root,

    //! The root of the mailbox
    root,

    //! The Junk E-mail folder
    junk_email,

    //! The Search Folders folder, also known as the Finder folder
    search_folders,

    //! The Voicemail folder
    voice_mail,

    //! Following are folders containing recoverable items:

    //! The root of the Recoverable Items folder hierarchy
    recoverable_items_root,

    //! The root of the folder hierarchy of recoverable items that have been
    //! soft-deleted from the Deleted Items folder
    recoverable_items_deletions,

    //! The root of the Recoverable Items versions folder hierarchy in the
    //! archive mailbox
    recoverable_items_versions,

    //! The root of the folder hierarchy of recoverable items that have been
    //! hard-deleted from the Deleted Items folder
    recoverable_items_purges,

    //! The root of the folder hierarchy in the archive mailbox
    archive_root,

    //! The root of the message folder hierarchy in the archive mailbox
    archive_msg_folder_root,

    //! The Deleted Items folder in the archive mailbox
    archive_deleted_items,

    //! Represents the archive Inbox folder. Caution: only versions of
    //! Exchange starting with build number 15.00.0913.09 include this
    //! folder
    archive_inbox,

    //! The root of the Recoverable Items folder hierarchy in the archive
    //! mailbox
    archive_recoverable_items_root,

    //! The root of the folder hierarchy of recoverable items that have been
    //! soft-deleted from the Deleted Items folder of the archive mailbox
    archive_recoverable_items_deletions,

    //! The root of the Recoverable Items versions folder hierarchy in the
    //! archive mailbox
    archive_recoverable_items_versions,

    //! The root of the hierarchy of recoverable items that have been
    //! hard-deleted from the Deleted Items folder of the archive mailbox
    archive_recoverable_items_purges,

    // Following are folders that came with EWS 2013 and Exchange Online:

    //! The Sync Issues folder
    sync_issues,

    //! The Conflicts folder
    conflicts,

    //! The Local Failures folder
    local_failures,

    //! Represents the Server Failures folder
    server_failures,

    //! The recipient cache folder
    recipient_cache,

    //! The quick contacts folder
    quick_contacts,

    //! The conversation history folder
    conversation_history,

    //! Represents the admin audit logs folder
    admin_audit_logs,

    //! The todo search folder
    todo_search,

    //! Represents the My Contacts folder
    my_contacts,

    //! Represents the directory folder
    directory,

    //! Represents the IM contact list folder
    im_contact_list,

    //! Represents the people connect folder
    people_connect,

    //! Represents the Favorites folder
    favorites
};

//! Contains the internal and external EWS URL when using Autodiscover.
//!
//! \sa get_exchange_web_services_url
struct autodiscover_result
{
    std::string internal_ews_url;
    std::string external_ews_url;
};

struct autodiscover_hints
{
    std::string autodiscover_url;
};

//! This enumeration indicates the sensitivity of an item.
enum class sensitivity
{
    //! The item has a normal sensitivity.
    normal,
    //! The item is personal.
    personal,
    //! The item is private.
    priv,
    //! The item is confidential.
    confidential
};

namespace internal
{
    inline std::string enum_to_str(sensitivity s)
    {
        switch (s)
        {
        case sensitivity::normal:
            return "Normal";
        case sensitivity::personal:
            return "Personal";
        case sensitivity::priv:
            return "Private";
        case sensitivity::confidential:
            return "Confidential";
        default:
            throw exception("Bad enum value");
        }
    }

    inline sensitivity str_to_sensitivity(const std::string& str)
    {
        if (str == "Normal")
        {
            return sensitivity::normal;
        }
        else if (str == "Personal")
        {
            return sensitivity::personal;
        }
        else if (str == "Private")
        {
            return sensitivity::priv;
        }
        else if (str == "Confidential")
        {
            return sensitivity::confidential;
        }
        else
        {
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! \brief This enumeration indicates the importance of an item.
//!
//! Valid values are Low, Normal, High.
enum class importance
{
    //! Low importance
    low,

    //! Normal importance
    normal,

    //! High importance
    high
};

namespace internal
{
    inline std::string enum_to_str(importance i)
    {
        switch (i)
        {
        case importance::low:
            return "Low";
        case importance::normal:
            return "Normal";
        case importance::high:
            return "High";
        default:
            throw exception("Bad enum value");
        }
    }

    inline importance str_to_importance(const std::string& str)
    {
        if (str == "Low")
        {
            return importance::low;
        }
        else if (str == "High")
        {
            return importance::high;
        }
        else if (str == "Normal")
        {
            return importance::normal;
        }
        else
        {
            throw exception("Bad enum value");
        }
    }

    struct response_result
    {
        response_class cls;
        response_code code;
        std::string message;

        response_result(response_class cls_, response_code code_)
            : cls(cls_), code(code_), message()
        {
        }

        response_result(response_class cls_, response_code code_,
                        std::string&& msg)
            : cls(cls_), code(code_), message(std::move(msg))
        {
        }
    };
} // namespace internal

//! Identifies the order and scope for a ResolveNames search.
enum class search_scope
{
    //! Only the Active Directory directory service is searched.
    active_directory,
    //! Active Directory is searched first, and then the contact folders that
    //! are specified in the ParentFolderIds property are searched.
    active_directory_contacts,
    //! Only the contact folders that are identified by the ParentFolderIds
    //! property are searched.
    contacts,
    //! Contact folders that are identified by the ParentFolderIds property are
    //! searched first and then Active Directory is searched.
    contacts_active_directory
};

namespace internal
{
    inline std::string enum_to_str(search_scope s)
    {
        switch (s)
        {
        case search_scope::active_directory:
            return "ActiveDirectory";
        case search_scope::active_directory_contacts:
            return "ActiveDirectoryContacts";
        case search_scope::contacts:
            return "Contacts";
        case search_scope::contacts_active_directory:
            return "ContactsActiveDirectory";
        default:
            throw exception("Bad enum value");
        }
    }

    inline search_scope str_to_search_scope(const std::string& str)
    {
        if (str == "ActiveDirectory")
        {
            return search_scope::active_directory;
        }
        else if (str == "ActiveDirectoryContacts")
        {
            return search_scope::active_directory_contacts;
        }
        else if (str == "Contacts")
        {
            return search_scope::contacts;
        }
        else if (str == "ContactsActiveDirectory")
        {
            return search_scope::contacts_active_directory;
        }
        else
        {
            throw exception("Bad enum value");
        }
    }
} // namespace internal

#ifdef EWS_HAS_VARIANT
//! Identifies the type of event returned or to subscribe to.
enum class event_type
{
    //! An event in which an item or folder is copied.
    copied_event,

    //! An event in which an item or folder is created.
    created_event,

    //! An event in which an item or folder is deleted.
    deleted_event,

    //! An event in which an item or folder is modified.
    modified_event,

    //! An event in which an item or folder is moved from one parent folder to
    //! another parent folder.
    moved_event,

    //! An event that is triggered by a new mail item in a mailbox.
    new_mail_event,
    //! A notification that no new activity has occurred in the mailbox.
    status_event,

    //! An event in which an item’s free/busy time has changed.
    free_busy_changed_event
};

namespace internal
{
    inline std::string enum_to_str(event_type e)
    {
        switch (e)
        {
        case event_type::copied_event:
            return "CopiedEvent";
        case event_type::created_event:
            return "CreatedEvent";
        case event_type::deleted_event:
            return "DeletedEvent";
        case event_type::modified_event:
            return "ModifiedEvent";
        case event_type::moved_event:
            return "MovedEvent";
        case event_type::new_mail_event:
            return "NewMailEvent";
        case event_type::status_event:
            return "StatusEvent";
        case event_type::free_busy_changed_event:
            return "FreeBusyChangedEvent";
        default:
            throw exception("Bad enum value");
        }
    }

    inline event_type str_to_event_type(const std::string& str)
    {
        if (str == "CopiedEvent")
        {
            return event_type::copied_event;
        }
        else if (str == "CreatedEvent")
        {
            return event_type::created_event;
        }
        else if (str == "DeletedEvent")
        {
            return event_type::deleted_event;
        }
        else if (str == "ModifiedEvent")
        {
            return event_type::modified_event;
        }
        else if (str == "MovedEvent")
        {
            return event_type::moved_event;
        }
        else if (str == "NewMailEvent")
        {
            return event_type::new_mail_event;
        }
        else if (str == "StatusEvent")
        {
            return event_type::status_event;
        }
        else if (str == "FreeBusyChangedEvent")
        {
            return event_type::free_busy_changed_event;
        }
        else
        {
            throw exception("Bad enum value");
        }
    }
} // namespace internal
#endif

//! Exception thrown when a request was not successful
class exchange_error final : public exception
{
public:
    //! Constructs an exchange_error from a \<ResponseCode>.
    explicit exchange_error(response_code code)
        : exception(internal::enum_to_str(code)), code_(code)
    {
    }

    //! \brief Constructs an exchange_error from a \<ResponseCode> and
    //! additional error message, e.g., from an \<MessageText> element.
    exchange_error(response_code code, const std::string& message_text)
        : exception(message_text.empty()
                        ? internal::enum_to_str(code)
                        : sanitize(message_text) + " (" +
                              internal::enum_to_str(code) + ")"),
          code_(code)
    {
    }

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
    explicit exchange_error(const internal::response_result& res)
        : exception(res.message.empty()
                        ? internal::enum_to_str(res.code)
                        : sanitize(res.message) + " (" +
                              internal::enum_to_str(res.code) + ")"),
          code_(res.code)
    {
    }
#endif

    response_code code() const EWS_NOEXCEPT { return code_; }

private:
    std::string sanitize(const std::string& message_text)
    {
        // Remove trailing dot, if any
        if (!message_text.empty() || message_text.back() == '.')
        {
            std::string tmp(message_text);
            tmp.pop_back();
            return tmp;
        }

        return message_text;
    }

    response_code code_;
};

namespace internal
{
    inline std::string http_status_code_to_str(int status_code)
    {
        switch (status_code)
        {
        case 100:
            return "Continue";
        case 101:
            return "Switching Protocols";
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 202:
            return "Accepted";
        case 203:
            return "Non-Authoritative Information";
        case 204:
            return "No Content";
        case 205:
            return "Reset Content";
        case 206:
            return "Partial Content";
        case 300:
            return "Multiple Choices";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 303:
            return "See Other";
        case 304:
            return "Not Modified";
        case 305:
            return "Use Proxy";
        case 307:
            return "Temporary Redirect";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 402:
            return "Payment Required";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 406:
            return "Not Acceptable";
        case 407:
            return "Proxy Authentication Required";
        case 408:
            return "Request Timeout";
        case 409:
            return "Conflict";
        case 410:
            return "Gone";
        case 411:
            return "Length Required";
        case 412:
            return "Precondition Failed";
        case 413:
            return "Request Entity Too Large";
        case 414:
            return "Request-URI Too Long";
        case 415:
            return "Unsupported Media Type";
        case 416:
            return "Requested Range Not Satisfiable";
        case 417:
            return "Expectation Failed";
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        case 505:
            return "HTTP Version Not Supported";
        }
        return "";
    }
} // namespace internal

//! Exception thrown when a HTTP request was not successful
class http_error final : public exception
{
public:
    explicit http_error(long status_code)
        : exception("HTTP status code: " + std::to_string(status_code) + " (" +
                    internal::http_status_code_to_str(status_code) + ")"),
          code_(status_code)
    {
    }

    long code() const EWS_NOEXCEPT { return code_; }

private:
    long code_;
};

//! A SOAP fault occurred due to a bad request
class soap_fault : public exception
{
public:
    explicit soap_fault(const std::string& what) : exception(what) {}
    explicit soap_fault(const char* what) : exception(what) {}
};

//! \brief A SOAP fault that is raised when we sent invalid XML.
//!
//! This is an internal error and indicates a bug in this library, thus
//! should never happen.
//!
//! Note: because this exception is due to a SOAP fault (sometimes
//! recognized before any server-side XML parsing finished) any included
//! failure message is likely not localized according to any MailboxCulture
//! SOAP header.
class schema_validation_error final : public soap_fault
{
public:
    schema_validation_error(unsigned long line_number,
                            unsigned long line_position, std::string violation)
        : soap_fault("The request failed schema validation"),
          violation_(std::move(violation)), line_pos_(line_position),
          line_no_(line_number)
    {
    }

    //! Line number in request string where the error was found
    unsigned long line_number() const EWS_NOEXCEPT { return line_no_; }

    //! Column number in request string where the error was found
    unsigned long line_position() const EWS_NOEXCEPT { return line_pos_; }

    //! A more detailed explanation of what went wrong
    const std::string& violation() const EWS_NOEXCEPT { return violation_; }

private:
    std::string violation_;
    unsigned long line_pos_;
    unsigned long line_no_;
};

//! \brief Exception for libcurl related runtime errors
class curl_error final : public exception
{
public:
    explicit curl_error(const std::string& what,
                        const std::string& error_details = "")
        : exception(what), error_details_(error_details)
    {
    }
    explicit curl_error(const char* what, const std::string& error_details = "")
        : exception(what), error_details_(error_details)
    {
    }

    //! More detailed information about the error
    const std::string& error_details() const EWS_NOEXCEPT
    {
        return error_details_;
    }

private:
    std::string error_details_;
};

namespace internal
{
    // Helper function; constructs an exception with a meaningful error
    // message from the given result code for the most recent cURL API call.
    //
    // msg: A string that prepends the actual cURL error message.
    // rescode: The result code of a failed cURL operation.
    inline curl_error make_curl_error(const std::string& msg, CURLcode rescode,
                                      const std::string& error_details = "")
    {
        auto reason = std::string(curl_easy_strerror(rescode));
#ifdef NDEBUG
        (void)msg;
        return curl_error(reason, error_details);
#else
        return curl_error(msg + ": \'" + reason + "\'", error_details);
#endif
    }

    // RAII helper class for CURL* handles.
    class curl_ptr final
    {
    public:
        curl_ptr() : handle_(curl_easy_init())
        {
            if (!handle_)
            {
                throw curl_error("Could not start libcurl session");
            }

            curl_easy_setopt(handle_, CURLOPT_ERRORBUFFER, error_details_);
        }

        curl_ptr(const curl_ptr& other)
            : handle_(curl_easy_duphandle(other.handle_))
        {
            if (!handle_)
            {
                throw curl_error("Could not dup curl handle");
            }

            curl_easy_setopt(handle_, CURLOPT_ERRORBUFFER, error_details_);
        }

        ~curl_ptr() { curl_easy_cleanup(handle_); }

#ifdef EWS_HAS_DEFAULT_AND_DELETE
        curl_ptr& operator=(const curl_ptr&) = delete;
#else
    private:
        curl_ptr& operator=(const curl_ptr&); // Never defined

    public:
#endif

        curl_ptr(curl_ptr&& other) : handle_(std::move(other.handle_))
        {
            other.handle_ = nullptr;
            curl_easy_setopt(handle_, CURLOPT_ERRORBUFFER, error_details_);
        }

        curl_ptr& operator=(curl_ptr&& rhs)
        {
            if (&rhs != this)
            {
                handle_ = std::move(rhs.handle_);
                rhs.handle_ = nullptr;
                curl_easy_setopt(handle_, CURLOPT_ERRORBUFFER, error_details_);
            }
            return *this;
        }

        CURL* get() const EWS_NOEXCEPT { return handle_; }

        const char* get_error_details() const EWS_NOEXCEPT
        {
            return error_details_;
        }

    private:
        CURL* handle_;
        char error_details_[CURL_ERROR_SIZE];
    };

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
    static_assert(std::is_default_constructible<curl_ptr>::value);
    static_assert(std::is_copy_constructible<curl_ptr>::value);
    static_assert(!std::is_copy_assignable<curl_ptr>::value);
    static_assert(std::is_move_constructible<curl_ptr>::value);
    static_assert(std::is_move_assignable<curl_ptr>::value);
#endif

    // RAII wrapper class around cURLs slist construct.
    class curl_string_list final
    {
    public:
        curl_string_list() EWS_NOEXCEPT : slist_(nullptr) {}

        ~curl_string_list() { curl_slist_free_all(slist_); }

#ifdef EWS_HAS_DEFAULT_AND_DELETE
        curl_string_list(const curl_string_list&) = delete;
        curl_string_list& operator=(const curl_string_list&) = delete;
#else
    private:
        curl_string_list(const curl_string_list&);            // N/A
        curl_string_list& operator=(const curl_string_list&); // N/A

    public:
#endif

        curl_string_list(curl_string_list&& other)
            : slist_(std::move(other.slist_))
        {
            other.slist_ = nullptr;
        }

        curl_string_list& operator=(curl_string_list&& rhs)
        {
            if (&rhs != this)
            {
                slist_ = std::move(rhs.slist_);
                rhs.slist_ = nullptr;
            }
            return *this;
        }

        void append(const char* str) EWS_NOEXCEPT
        {
            slist_ = curl_slist_append(slist_, str);
        }

        curl_slist* get() const EWS_NOEXCEPT { return slist_; }

    private:
        curl_slist* slist_;
    };

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
    static_assert(std::is_default_constructible<curl_string_list>::value);
    static_assert(!std::is_copy_constructible<curl_string_list>::value);
    static_assert(!std::is_copy_assignable<curl_string_list>::value);
    static_assert(std::is_move_constructible<curl_string_list>::value);
    static_assert(std::is_move_assignable<curl_string_list>::value);
#endif

    // String constants
    // TODO: sure this can't be done easier within a header file?
    // We need better handling of static strings (URIs, XML node names,
    // values and stuff, see usage of rapidxml::internal::compare)
    template <typename Ch = char> struct uri
    {
        struct microsoft
        {
            enum
            {
                errors_size = 58U
            };
            static const Ch* errors()
            {
                static const Ch* val = "http://schemas.microsoft.com/"
                                       "exchange/services/2006/errors";
                return val;
            }
            enum
            {
                types_size = 57U
            };
            static const Ch* types()
            {
                static const Ch* const val = "http://schemas.microsoft.com/"
                                             "exchange/services/2006/types";
                return val;
            }
            enum
            {
                messages_size = 60U
            };
            static const Ch* messages()
            {
                static const Ch* const val = "http://schemas.microsoft.com/"
                                             "exchange/services/2006/"
                                             "messages";
                return val;
            }
            enum
            {
                autodiscover_size = 79U
            };
            static const Ch* autodiscover()
            {
                static const Ch* const val = "http://schemas.microsoft.com/"
                                             "exchange/autodiscover/"
                                             "outlook/responseschema/2006a";
                return val;
            }
        };

        struct soapxml
        {
            enum
            {
                envelope_size = 41U
            };
            static const Ch* envelope()
            {
                static const Ch* const val =
                    "http://schemas.xmlsoap.org/soap/envelope/";
                return val;
            }
        };
    };

    // This ought to be a DOM wrapper; usually around a web response
    //
    // This class basically wraps rapidxml::xml_document because the parsed
    // data must persist for the lifetime of the rapidxml::xml_document.
    class http_response final
    {
    public:
        http_response(long code, std::vector<char>&& data)
            : data_(std::move(data)), code_(code)
        {
            check(!data_.empty(), "Given data should not be empty");
        }

#ifdef EWS_HAS_DEFAULT_AND_DELETE
        ~http_response() = default;

        http_response(const http_response&) = delete;
        http_response& operator=(const http_response&) = delete;
#else
        http_response() {}

    private:
        http_response(const http_response&);            // Never defined
        http_response& operator=(const http_response&); // Never defined

    public:
#endif

        http_response(http_response&& other)
            : data_(std::move(other.data_)), code_(std::move(other.code_))
        {
            other.code_ = 0U;
        }

        http_response& operator=(http_response&& rhs)
        {
            if (&rhs != this)
            {
                data_ = std::move(rhs.data_);
                code_ = std::move(rhs.code_);
            }
            return *this;
        }

        // Returns a reference to the raw byte content in this HTTP
        // response.
        std::vector<char>& content() EWS_NOEXCEPT { return data_; }

        // Returns a reference to the raw byte content in this HTTP
        // response.
        const std::vector<char>& content() const EWS_NOEXCEPT { return data_; }

        // Returns the response code of the HTTP request.
        long code() const EWS_NOEXCEPT { return code_; }

        // Returns whether the response is a SOAP fault.
        //
        // This means the server responded with status code 500 and
        // indicates that the entire request failed (not just a normal EWS
        // error). This can happen e.g. when the request we sent was not
        // schema compliant.
        bool is_soap_fault() const EWS_NOEXCEPT { return code() == 500U; }

        // Returns whether the HTTP response code is 200 (OK).
        bool ok() const EWS_NOEXCEPT { return code() == 200U; }

    private:
        std::vector<char> data_;
        long code_;
    };

    // Loads the XML content from a given HTTP response into a
    // new xml_document and returns it.
    //
    // Note: we are using a mutable temporary buffer internally because
    // we are using RapidXml in destructive mode (the parser modifies
    // source text during the parsing process). Hence, we need to make
    // sure that parsing is done only once!
    inline std::unique_ptr<rapidxml::xml_document<char>>
    parse_response(http_response&& response)
    {
        if (response.content().empty())
        {
            throw xml_parse_error("Cannot parse empty response");
        }

#ifdef EWS_HAS_MAKE_UNIQUE
        auto doc = std::make_unique<rapidxml::xml_document<char>>();
#else
        auto doc = std::unique_ptr<rapidxml::xml_document<char>>(
            new rapidxml::xml_document<char>());
#endif
        try
        {
            static const int flags = 0;
            doc->parse<flags>(&response.content()[0]);
        }
        catch (rapidxml::parse_error& exc)
        {
            // Swallow and erase type
            const auto msg =
                xml_parse_error::error_message_from(exc, response.content());
            throw xml_parse_error(msg);
        }

#ifdef EWS_ENABLE_VERBOSE
        std::cerr << "Response code: " << response.code() << ", Content:\n\'"
                  << *doc << "\'" << std::endl;
#endif

        return doc;
    }

    // TODO: explicitly for nodes in Types XML namespace, document or
    // change interface
    inline rapidxml::xml_node<>& create_node(rapidxml::xml_node<>& parent,
                                             const std::string& name)
    {
        auto doc = parent.document();
        check(doc, "Parent node needs to be part of a document");
        auto ptr_to_qname = doc->allocate_string(name.c_str());
        auto new_node = doc->allocate_node(rapidxml::node_element);
        new_node->qname(ptr_to_qname, name.length(), ptr_to_qname + 2);
        new_node->namespace_uri(internal::uri<>::microsoft::types(),
                                internal::uri<>::microsoft::types_size);
        parent.append_node(new_node);
        return *new_node;
    }

    inline rapidxml::xml_node<>& create_node(rapidxml::xml_node<>& parent,
                                             const std::string& name,
                                             const std::string& value)
    {
        auto doc = parent.document();
        check(doc, "Parent node needs to be part of a document");
        auto str = doc->allocate_string(value.c_str());
        auto& new_node = create_node(parent, name);
        new_node.value(str);
        return new_node;
    }

    // Traverse elements, depth first, beginning with given node.
    //
    // Applies given function to every element during traversal, stopping as
    // soon as that function returns true.
    template <typename Function>
    inline bool traverse_elements(const rapidxml::xml_node<>& node,
                                  Function func) EWS_NOEXCEPT
    {
        for (auto child = node.first_node(); child != nullptr;
             child = child->next_sibling())
        {
            if (traverse_elements(*child, func))
            {
                return true;
            }

            if (child->type() == rapidxml::node_element)
            {
                if (func(*child))
                {
                    return true;
                }
            }
        }
        return false;
    }

    // Select element by qualified name, nullptr if there is no such element
    // TODO: how to avoid ambiguities when two elements exist with the same
    // name and namespace but different paths
    inline rapidxml::xml_node<>*
    get_element_by_qname(const rapidxml::xml_node<>& node,
                         const char* local_name, const char* namespace_uri)
    {
        check(local_name, "Expected local_name, got nullptr");
        check(namespace_uri, "Expected namespace_uri, got nullptr");

        rapidxml::xml_node<>* element = nullptr;
        const auto local_name_len = strlen(local_name);
        const auto namespace_uri_len = strlen(namespace_uri);
        traverse_elements(node, [&](rapidxml::xml_node<>& elem) -> bool {
            using rapidxml::internal::compare;

            if (compare(elem.namespace_uri(), elem.namespace_uri_size(),
                        namespace_uri, namespace_uri_len) &&
                compare(elem.local_name(), elem.local_name_size(), local_name,
                        local_name_len))
            {
                element = std::addressof(elem);
                return true;
            }
            return false;
        });
        return element;
    }

    class credentials
    {
    public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
        virtual ~credentials() = default;
        credentials& operator=(const credentials&) = default;
#else
        virtual ~credentials() {}
#endif
        virtual void certify(http_request*) const = 0;

        virtual std::unique_ptr<credentials> clone() const = 0;
    };

    //! \brief This class a basic implementation for OAuth2 grant types.
    class oauth2_basic : public credentials
    {
    public:
        oauth2_basic(std::string tenant, std::string client_id,
                     std::string scope)
            : tenant_(std::move(tenant)), client_id_(std::move(client_id)),
              scope_(std::move(scope))
        {
        }

#ifdef EWS_HAS_DEFAULT_AND_DELETE
        virtual ~oauth2_basic() = default;
        oauth2_basic& operator=(const oauth2_basic&) = default;
#else
        virtual ~oauth2_basic() {}
#endif

        void certify(internal::http_request*) const override;

        bool expired() const
        {
            return (std::chrono::steady_clock::now() > expiration);
        }

    protected:
        //! \brief Adds authentication information to the request
        //! \param request The request that should be authenticated
        virtual void authenticate(internal::http_request* request) const;

        virtual void append_url(CURL* c, std::string& data) const = 0;

    private:
        std::string tenant_;
        std::string client_id_;
        std::string scope_;
        mutable std::string access_token;
        mutable std::chrono::steady_clock::time_point expiration;
    };

} // namespace internal

//! \brief This class allows HTTP basic authentication.
//!
//! Basic authentication allows a client application to authenticate with
//! username and password. \b Important: Because the password is transmitted
//! to the server in plain-text, this method is \b not secure unless you
//! provide some form of transport layer security.
//!
//! However, basic authentication can be the correct choice for your
//! application in some circumstances, e.g., for debugging purposes or if you
//! have a proxy in between that does not support NTLM, if your application
//! communicates via TLS encrypted HTTP.
class basic_credentials final : public internal::credentials
{
public:
    basic_credentials(std::string username, std::string password)
        : username_(std::move(username)), password_(std::move(password))
    {
    }

    std::unique_ptr<internal::credentials> clone() const override
    {
#ifdef EWS_HAS_MAKE_UNIQUE
        return std::make_unique<basic_credentials>(*this);
#else
        return std::unique_ptr<basic_credentials>(new basic_credentials(*this));
#endif
    }

private:
    // Implemented below
    void certify(internal::http_request*) const override;

    std::string username_;
    std::string password_;
};

//! \brief This class allows NTLM authentication.
//!
//! NTLM authentication is only available for Exchange on-premises servers.
//!
//! For applications that run inside the corporate firewall, NTLM
//! authentication provides a built-in means to authenticate against an
//! Exchange server. However, because NTLM requires the client to store the
//! user's password in plain-text for the entire session, it is not the
//! safest method to use.
class ntlm_credentials final : public internal::credentials
{
public:
    ntlm_credentials(std::string username, std::string password,
                     std::string domain)
        : username_(std::move(username)), password_(std::move(password)),
          domain_(std::move(domain))
    {
    }

    std::unique_ptr<internal::credentials> clone() const override
    {
#ifdef EWS_HAS_MAKE_UNIQUE
        return std::make_unique<ntlm_credentials>(*this);
#else
        return std::unique_ptr<ntlm_credentials>(new ntlm_credentials(*this));
#endif
    }

private:
    // Implemented below
    void certify(internal::http_request*) const override;

    std::string username_;
    std::string password_;
    std::string domain_;
};

//! \brief This class allows OAuth2 authentification using the client
//! credentials grant type.
//!
class oauth2_client_credentials final : public internal::oauth2_basic
{
public:
    oauth2_client_credentials(std::string tenant, std::string client_id,
                              std::string client_secret, std::string scope,
                              std::string resource)
        : oauth2_basic(std::move(tenant), std::move(client_id),
                       std::move(scope)),
          client_secret_(std::move(client_secret)),
          resource_(std::move(resource))
    {
    }

    std::unique_ptr<internal::credentials> clone() const override
    {
#ifdef EWS_HAS_MAKE_UNIQUE
        return std::make_unique<oauth2_client_credentials>(*this);
#else
        return std::unique_ptr<oauth2_client_credentials>(
            new oauth2_client_credentials(*this));
#endif
    }

private:
    void append_url(CURL* c, std::string& data) const override;

    const std::string client_secret_;
    const std::string resource_;
};

//! \brief This class allows OAuth2 authentification using the Ressource Owner
//! Password
//!        Credentials grant.
class oauth2_resource_owner_password_credentials final
    : public internal::oauth2_basic
{
public:
    oauth2_resource_owner_password_credentials(
        std::string tenant, std::string client_id, std::string client_secret,
        std::string scope, std::string username, std::string password)
        : oauth2_basic(std::move(tenant), std::move(client_id),
                       std::move(scope)),
          username_(std::move(username)), password_(std::move(password)),
          client_secret_(std::move(client_secret))
    {
    }

    std::unique_ptr<internal::credentials> clone() const override
    {
#ifdef EWS_HAS_MAKE_UNIQUE
        return std::make_unique<oauth2_resource_owner_password_credentials>(
            *this);
#else
        return std::unique_ptr<oauth2_resource_owner_password_credentials>(
            new oauth2_resource_owner_password_credentials(*this));
#endif
    }

private:
    void append_url(CURL* c, std::string& data) const override;

    const std::string username_;
    const std::string password_;
    const std::string client_secret_;
};

namespace internal
{
    class http_request final
    {
    public:
        enum class method
        {
            POST
        };

        // Create a new HTTP request to the given URL.
        explicit http_request(const std::string& url)
        {
            set_option(CURLOPT_URL, url.c_str());
        }

        //! \param handle Handle whose settings should be used
        explicit http_request(const curl_ptr& handle) : handle_(handle)
        {
            // empty
        }

        //! \return The internal handle
        const curl_ptr& get_handle() const { return handle_; }

        // Set the HTTP method (only POST supported).
        void set_method(method)
        {
            // Method can only be a regular POST in our use case
            set_option(CURLOPT_POST, 1);
        }

        // Set this HTTP request's content type.
        void set_content_type(const std::string& content_type)
        {
            const std::string str = "Content-Type: " + content_type;
            headers_.append(str.c_str());
        }

        // Set this HTTP request's content length.
        void set_content_length(size_t content_length)
        {
            const std::string str =
                "Content-Length: " + std::to_string(content_length);
            headers_.append(str.c_str());
        }

        void set_authorization(const std::string& authorization)
        {
            const std::string str = "Authorization: " + authorization;
            headers_.append(str.c_str());
        }

        // Set credentials for authentication.
        void set_credentials(const credentials& creds) { creds.certify(this); }

        void set_timeout(std::chrono::seconds timeout)
        {
            curl_easy_setopt(handle_.get(), CURLOPT_TIMEOUT, timeout.count());
        }

        void set_expect(const std::string& value)
        {
            const std::string str = "Expect: " + value;
            headers_.append(str.c_str());
        }

#ifdef EWS_HAS_VARIADIC_TEMPLATES
        // Small wrapper around curl_easy_setopt(3).
        //
        // Converts return codes to exceptions of type curl_error and allows
        // objects other than http_web_requests to set transfer options
        // without direct access to the internal CURL handle.
        template <typename... Args>
        void set_option(CURLoption option, Args... args)
        {
            auto retcode = curl_easy_setopt(handle_.get(), option,
                                            std::forward<Args>(args)...);
            switch (retcode)
            {
            case CURLE_OK:
                return;

            case CURLE_FAILED_INIT:
            {
                throw make_curl_error("curl_easy_setopt: unsupported option",
                                      retcode);
            }

            default:
            {
                throw make_curl_error("curl_easy_setopt: failed setting option",
                                      retcode);
            }
            };
        }
#else
        template <typename T1> void set_option(CURLoption option, T1 arg1)
        {
            auto retcode = curl_easy_setopt(handle_.get(), option, arg1);
            switch (retcode)
            {
            case CURLE_OK:
                return;

            case CURLE_FAILED_INIT:
            {
                throw make_curl_error("curl_easy_setopt: unsupported option",
                                      retcode);
            }

            default:
            {
                throw make_curl_error("curl_easy_setopt: failed setting option",
                                      retcode);
            }
            };
        }

        template <typename T1, typename T2>
        void set_option(CURLoption option, T1 arg1, T2 arg2)
        {
            auto retcode = curl_easy_setopt(handle_.get(), option, arg1, arg2);
            switch (retcode)
            {
            case CURLE_OK:
                return;

            case CURLE_FAILED_INIT:
            {
                throw make_curl_error("curl_easy_setopt: unsupported option",
                                      retcode);
            }

            default:
            {
                throw make_curl_error("curl_easy_setopt: failed setting option",
                                      retcode);
            }
            };
        }
#endif

        // Perform the HTTP request and returns the response. This function
        // blocks until the complete response is received or a timeout is
        // reached. Throws curl_error if operation could not be completed.
        //
        // request: The complete request string; you must make sure that
        // the data is encoded the way you want the server to receive it.
        http_response send(const std::string& request)
        {
            auto callback = [](char* ptr, size_t size, size_t nmemb,
                               void* userdata) -> size_t {
                std::vector<char>* buf =
                    reinterpret_cast<std::vector<char>*>(userdata);
                const auto realsize = size * nmemb;
                try
                {
                    buf->reserve(realsize + 1); // plus 0-terminus
                }
                catch (std::bad_alloc&)
                {
                    // Out of memory, indicate error to libcurl
                    return 0U;
                }
                std::copy(ptr, ptr + realsize, std::back_inserter(*buf));
                return realsize;
            };

            // Do not install (directly or indirectly) signal handlers nor
            // call any functions that cause signals to be sent to the
            // process
            // Note: SIGCHLD is raised anyway if we use CURLAUTH_NTLM_WB and
            // SIGPIPE is still possible, too
            set_option(CURLOPT_NOSIGNAL, 1L);

#ifdef EWS_ENABLE_VERBOSE
            // Print HTTP headers to stderr
            set_option(CURLOPT_VERBOSE, 1L);
#endif

            // Set complete request string for HTTP POST method; note: no
            // encoding here
            set_option(CURLOPT_POSTFIELDS, request.c_str());
            set_option(CURLOPT_POSTFIELDSIZE, request.length());

            // Finally, set HTTP headers. We do this as last action here
            // because we want to overwrite implicitly set header lines due
            // to the options set above with our own header lines
            set_option(CURLOPT_HTTPHEADER, headers_.get());

            std::vector<char> response_data;
            set_option(CURLOPT_WRITEFUNCTION,
                       static_cast<size_t (*)(char*, size_t, size_t, void*)>(
                           callback));
            set_option(CURLOPT_WRITEDATA, std::addressof(response_data));

#ifdef EWS_DISABLE_TLS_CERT_VERIFICATION
            // Warn in release builds
#    ifdef NDEBUG
#        pragma message(                                                       \
            "warning: TLS verification of the server's authenticity is disabled")
#    endif
            // Turn-off verification of the server's authenticity
            set_option(CURLOPT_SSL_VERIFYPEER, 0L);
            set_option(CURLOPT_SSL_VERIFYHOST, 0L);

#    if LIBCURL_VERSION_NUM >= 0x073400
            set_option(CURLOPT_PROXY_SSL_VERIFYPEER, 0L);
            set_option(CURLOPT_PROXY_SSL_VERIFYHOST, 0L);
#    endif

#endif

            auto retcode = curl_easy_perform(handle_.get());
            if (retcode != 0)
            {
                throw make_curl_error("curl_easy_perform", retcode,
                                      handle_.get_error_details());
            }
            long response_code = 0U;
            curl_easy_getinfo(handle_.get(), CURLINFO_RESPONSE_CODE,
                              &response_code);
            response_data.emplace_back('\0');
            return http_response(std::move(response_code),
                                 std::move(response_data));
        }

    private:
        curl_ptr handle_;
        curl_string_list headers_;
    };

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
    static_assert(!std::is_default_constructible<http_request>::value);
    static_assert(!std::is_copy_constructible<http_request>::value);
    static_assert(!std::is_copy_assignable<http_request>::value);
    static_assert(std::is_move_constructible<http_request>::value);
    static_assert(std::is_move_assignable<http_request>::value);
#endif

#ifdef EWS_HAS_DEFAULT_TEMPLATE_ARGS_FOR_FUNCTIONS
    template <typename RequestHandler = http_request>
#else
    template <typename RequestHandler>
#endif
    inline http_response
    make_raw_soap_request(RequestHandler& handler, const std::string& soap_body,
                          const std::vector<std::string>& soap_headers)
    {
        std::stringstream request_stream;
        request_stream
            << "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
               "<soap:Envelope "
               "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
               "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
               "xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\" "
               "xmlns:m=\"http://schemas.microsoft.com/exchange/services/"
               "2006/messages\" "
               "xmlns:t=\"http://schemas.microsoft.com/exchange/services/"
               "2006/types\">";

        if (!soap_headers.empty())
        {
            request_stream << "<soap:Header>";
            for (const auto& header : soap_headers)
            {
                request_stream << header;
            }
            request_stream << "</soap:Header>";
        }

        request_stream << "<soap:Body>";
        request_stream << soap_body;
        request_stream << "</soap:Body>";
        request_stream << "</soap:Envelope>";

#ifdef EWS_ENABLE_VERBOSE
        std::cerr << "make_raw_soap_request: " << std::endl << request_stream.str() << std::endl;
#endif
        return handler.send(request_stream.str());
    }
// Makes a raw SOAP request.
//
// url: The URL of the server to talk to.
// username: The user-name of user.
// password: The user's secret password, plain-text.
// domain: The user's Windows domain.
// soap_body: The contents of the SOAP body (minus the body element);
// this is the actual EWS request.
// soap_headers: Any SOAP headers to add.
//
// Returns the response.
#ifdef EWS_HAS_DEFAULT_TEMPLATE_ARGS_FOR_FUNCTIONS
    template <typename RequestHandler = http_request>
#else
    template <typename RequestHandler>
#endif
    inline http_response
    make_raw_soap_request(const std::string& url, const std::string& username,
                          const std::string& password,
                          const std::string& domain,
                          const std::string& soap_body,
                          const std::vector<std::string>& soap_headers)
    {
        RequestHandler handler(url);
        handler.set_method(RequestHandler::method::POST);
        handler.set_content_type("text/xml; charset=utf-8");
        handler.set_expect("");
        ntlm_credentials creds(username, password, domain);
        handler.set_credentials(creds);
        return make_raw_soap_request(handler, soap_body, soap_headers);
    }

    // A self-contained copy of a DOM sub-tree generally used to hold
    // properties of an item class
    //
    // We use rapidxml::print for copying the already parsed xml_document
    // into a new buffer and then re-parse this buffer again under the
    // assumption that all child elements are contained in
    //
    //     http://schemas.microsoft.com/exchange/services/2006/types
    //
    // XML namespace.
    //
    // A default constructed xml_subtree instance makes only sense when an
    // item class is default constructed. In that case the buffer (and the
    // DOM) is initially empty and elements are added directly to the
    // document's root node.
    class xml_subtree final
    {
    public:
        struct attribute
        {
            std::string name;
            std::string value;
        };

        // Default constructible because item class (and it's descendants)
        // need to be
        xml_subtree() : rawdata_(), doc_(new rapidxml::xml_document<char>) {}

        explicit xml_subtree(const rapidxml::xml_node<char>& origin,
                             size_t size_hint = 0U)
            : rawdata_(), doc_(new rapidxml::xml_document<char>)
        {
            reparse(origin, size_hint);
        }

        // Needs to be copy- and copy-assignable because item classes are.
        // However xml_document isn't (and can't be without major rewrite
        // IMHO).  Hence, copying is quite costly as it involves serializing
        // DOM tree to string and re-parsing into new tree
        xml_subtree& operator=(const xml_subtree& rhs)
        {
            if (&rhs != this)
            {
                // FIXME: not strong exception safe
                rawdata_.clear();
                doc_->clear();
                // TODO: can be done with deep_copy
                reparse(*rhs.doc_, rhs.rawdata_.size());
            }
            return *this;
        }

        xml_subtree(const xml_subtree& other)
            : rawdata_(), doc_(new rapidxml::xml_document<char>)
        {
            reparse(*other.doc_, other.rawdata_.size());
        }

        // Returns a pointer to the root node of this sub-tree. Returned
        // pointer can be null
        rapidxml::xml_node<>* root() EWS_NOEXCEPT { return doc_->first_node(); }

        // Returns a pointer to the root node of this sub-tree. Returned
        // pointer can be null
        const rapidxml::xml_node<>* root() const EWS_NOEXCEPT
        {
            return doc_->first_node();
        }

        // Might return nullptr when there is no such element. Client code
        // needs to check returned pointer. Should never throw
        rapidxml::xml_node<char>*
        get_node(const char* node_name) const EWS_NOEXCEPT
        {
            return get_element_by_qname(*doc_, node_name,
                                        uri<>::microsoft::types());
        }

        rapidxml::xml_node<char>*
        get_node(const std::string& node_name) const EWS_NOEXCEPT
        {
            return get_node(node_name.c_str());
        }

        rapidxml::xml_document<char>* document() const EWS_NOEXCEPT
        {
            return doc_.get();
        }

        std::string get_value_as_string(const char* node_name) const
        {
            const auto node = get_node(node_name);
            return node ? std::string(node->value(), node->value_size()) : "";
        }

        // Update an existing node with a new value. If the node does not exist
        // it is created. Note that any extisting attribute will get removed
        // from an existing node.
        rapidxml::xml_node<char>& set_or_update(const std::string& node_name,
                                                const std::string& node_value)
        {
            using rapidxml::internal::compare;

            auto oldnode = get_element_by_qname(*doc_, node_name.c_str(),
                                                uri<>::microsoft::types());
            if (oldnode && compare(node_value.c_str(), node_value.length(),
                                   oldnode->value(), oldnode->value_size()))
            {
                // Nothing to do
                return *oldnode;
            }

            const auto node_qname = "t:" + node_name;
            const auto ptr_to_qname = doc_->allocate_string(node_qname.c_str());
            const auto ptr_to_value = doc_->allocate_string(node_value.c_str());

            auto newnode = doc_->allocate_node(rapidxml::node_element);
            newnode->qname(ptr_to_qname, node_qname.length(), ptr_to_qname + 2);
            newnode->value(ptr_to_value);
            newnode->namespace_uri(uri<>::microsoft::types(),
                                   uri<>::microsoft::types_size);
            if (oldnode)
            {
                auto parent = oldnode->parent();
                parent->insert_node(oldnode, newnode);
                parent->remove_node(oldnode);
                return *oldnode;
            }

            doc_->append_node(newnode);
            return *newnode;
        }

        // Update an existing node with a set of attributes. If
        // the node does not exist it is created. Note that if the node already
        // exists, all extisting attributes will get removed and replaced by the
        // given set.
        rapidxml::xml_node<char>&
        set_or_update(const std::string& node_name,
                      const std::vector<attribute>& attributes)
        {
            using rapidxml::internal::compare;

            auto oldnode = get_element_by_qname(*doc_, node_name.c_str(),
                                                uri<>::microsoft::types());
            const auto node_qname = "t:" + node_name;
            const auto ptr_to_qname = doc_->allocate_string(node_qname.c_str());

            auto newnode = doc_->allocate_node(rapidxml::node_element);
            newnode->qname(ptr_to_qname, node_qname.length(), ptr_to_qname + 2);
            newnode->namespace_uri(uri<>::microsoft::types(),
                                   uri<>::microsoft::types_size);

            for (auto& attrib : attributes)
            {
                const auto name = doc_->allocate_string(attrib.name.c_str());
                const auto value = doc_->allocate_string(attrib.value.c_str());
                auto attr = doc_->allocate_attribute(name, value);
                newnode->append_attribute(attr);
            }

            if (oldnode)
            {
                auto parent = oldnode->parent();
                parent->insert_node(oldnode, newnode);
                parent->remove_node(oldnode);
                return *oldnode;
            }

            doc_->append_node(newnode);
            return *newnode;
        }

        std::string to_string() const
        {
            std::string str;
            rapidxml::print(std::back_inserter(str), *doc_,
                            rapidxml::print_no_indenting);
            return str;
        }

        void append_to(rapidxml::xml_node<>& dest) const
        {
            auto target_document = dest.document();
            auto source = doc_->first_node();
            if (source)
            {
                auto new_child = deep_copy(target_document, source);
                dest.append_node(new_child);
            }
        }

    private:
        std::vector<char> rawdata_;
        std::unique_ptr<rapidxml::xml_document<char>> doc_;

        // Custom namespace processor for parsing XML sub-tree. Makes
        // uri::microsoft::types the default namespace.
        struct custom_namespace_processor
            : public rapidxml::internal::xml_namespace_processor<char>
        {
            struct scope : public rapidxml::internal::xml_namespace_processor<
                               char>::scope
            {
                explicit scope(xml_namespace_processor& processor)
                    : rapidxml::internal::xml_namespace_processor<char>::scope(
                          processor)
                {
                    static auto nsattr = make_namespace_attribute();
                    add_namespace_prefix(&nsattr);
                }

                static rapidxml::xml_attribute<char> make_namespace_attribute()
                {
                    static const char* const name = "t";
                    static const char* const value = uri<>::microsoft::types();
                    rapidxml::xml_attribute<char> attr;
                    attr.name(name, 1);
                    attr.value(value, uri<>::microsoft::types_size);
                    return attr;
                }
            };
        };

        void reparse(const rapidxml::xml_node<char>& source, size_t size_hint)
        {
            rawdata_.reserve(size_hint);
            rapidxml::print(std::back_inserter(rawdata_), source,
                            rapidxml::print_no_indenting);
            rawdata_.emplace_back('\0');

            // Note: we use xml_document::parse_ns here. This is because we
            // substitute the default namespace processor with a custom one.
            // Reason we do this: when we re-parse only a sub-tree of the
            // original XML document, we loose all enclosing namespace URIs.

            doc_->parse_ns<0, custom_namespace_processor>(&rawdata_[0]);
        }

        static rapidxml::xml_node<>*
        deep_copy(rapidxml::xml_document<>* target_doc,
                  const rapidxml::xml_node<>* source)
        {
            check(target_doc, "Expected target_doc, got nullptr");
            check(source, "Expected source, got nullptr");

            auto newnode = target_doc->allocate_node(source->type());

            // Copy name and value
            auto name = target_doc->allocate_string(source->name());
            newnode->qname(name, source->name_size(), source->local_name());

            auto value = target_doc->allocate_string(source->value());
            newnode->value(value, source->value_size());

            // Copy child nodes and attributes
            for (auto child = source->first_node(); child;
                 child = child->next_sibling())
            {
                newnode->append_node(deep_copy(target_doc, child));
            }

            for (auto attr = source->first_attribute(); attr;
                 attr = attr->next_attribute())
            {
                newnode->append_attribute(target_doc->allocate_attribute(
                    attr->name(), attr->value(), attr->name_size(),
                    attr->value_size()));
            }

            return newnode;
        }
    };

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
    static_assert(std::is_default_constructible<xml_subtree>::value);
    static_assert(std::is_copy_constructible<xml_subtree>::value);
    static_assert(std::is_copy_assignable<xml_subtree>::value);
    static_assert(std::is_move_constructible<xml_subtree>::value);
    static_assert(std::is_move_assignable<xml_subtree>::value);
#endif

#ifdef EWS_HAS_DEFAULT_TEMPLATE_ARGS_FOR_FUNCTIONS
    template <typename RequestHandler = http_request>
#else
    template <typename RequestHandler>
#endif
    inline autodiscover_result
    get_exchange_web_services_url(const std::string& user_smtp_address,
                                  const basic_credentials& credentials,
                                  unsigned int redirections,
                                  const autodiscover_hints& hints)
    {
        using rapidxml::internal::compare;

        // Check redirection counter. We don't want to get in an endless
        // loop
        if (redirections > 2)
        {
            throw exception("Maximum of two redirections reached");
        }

        // Check SMTP address
        if (user_smtp_address.empty())
        {
            throw exception("Empty SMTP address given");
        }

        std::string autodiscover_url;
        if (hints.autodiscover_url.empty())
        {
            // Get user name and domain part from the SMTP address
            const auto at_sign_idx = user_smtp_address.find_first_of("@");
            if (at_sign_idx == std::string::npos)
            {
                throw exception("No valid SMTP address given");
            }

            const auto username = user_smtp_address.substr(0, at_sign_idx);
            const auto domain = user_smtp_address.substr(
                at_sign_idx + 1, user_smtp_address.size());

            // It is important that we use an HTTPS end-point here because we
            // authenticate with HTTP basic auth; specifically we send the
            // passphrase in plain-text
            autodiscover_url =
                "https://" + domain + "/autodiscover/autodiscover.xml";
        }
        else
        {
            autodiscover_url = hints.autodiscover_url;
        }
        // Create an Outlook provider <Autodiscover/> request
        std::stringstream sstr;
        sstr << "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
             << "<Autodiscover "
             << "xmlns=\"http://schemas.microsoft.com/exchange/"
                "autodiscover/outlook/requestschema/2006\">"
             << "<Request>"
             << "<EMailAddress>" << user_smtp_address << "</EMailAddress>"
             << "<AcceptableResponseSchema>"
             << internal::uri<>::microsoft::autodiscover()
             << "</AcceptableResponseSchema>"
             << "</Request>"
             << "</Autodiscover>";
        const auto request_string = sstr.str();

        RequestHandler handler(autodiscover_url);
        handler.set_method(RequestHandler::method::POST);
        handler.set_credentials(credentials);
        handler.set_content_type("text/xml; charset=utf-8");
        handler.set_content_length(request_string.size());
        handler.set_expect("");

#ifdef EWS_ENABLE_VERBOSE
        std::cerr << request_string << std::endl;
#endif

        auto response = handler.send(request_string);
        if (!response.ok())
        {
            throw http_error(response.code());
        }

        const auto doc = parse_response(std::move(response));

        const auto account_node = get_element_by_qname(
            *doc, "Account", internal::uri<>::microsoft::autodiscover());
        if (!account_node)
        {
            // Check for <Error/> element
            const auto error_node =
                get_element_by_qname(*doc, "Error",
                                     "http://schemas.microsoft.com/exchange/"
                                     "autodiscover/responseschema/2006");
            if (error_node)
            {
                std::string error_code;
                std::string message;

                for (auto node = error_node->first_node(); node;
                     node = node->next_sibling())
                {
                    if (compare(node->local_name(), node->local_name_size(),
                                "ErrorCode", strlen("ErrorCode")))
                    {
                        error_code =
                            std::string(node->value(), node->value_size());
                    }
                    else if (compare(node->local_name(),
                                     node->local_name_size(), "Message",
                                     strlen("Message")))
                    {
                        message =
                            std::string(node->value(), node->value_size());
                    }

                    if (!error_code.empty() && !message.empty())
                    {
                        throw exception(message +
                                        " (error code: " + error_code + ")");
                    }
                }
            }

            throw exception("Unable to parse response");
        }

        // For each protocol in the response, look for the appropriate
        // protocol type (internal/external) and then look for the
        // corresponding <ASUrl/> element
        std::string protocol;
        autodiscover_result result;
        for (int i = 0; i < 2; i++)
        {
            for (auto protocol_node = account_node->first_node(); protocol_node;
                 protocol_node = protocol_node->next_sibling())
            {
                if (i >= 1)
                {
                    protocol = "EXCH";
                }
                else
                {
                    protocol = "EXPR";
                }
                if (compare(protocol_node->local_name(),
                            protocol_node->local_name_size(), "Protocol",
                            strlen("Protocol")))
                {
                    for (auto type_node = protocol_node->first_node();
                         type_node; type_node = type_node->next_sibling())
                    {
                        if (compare(type_node->local_name(),
                                    type_node->local_name_size(), "Type",
                                    strlen("Type")) &&
                            compare(type_node->value(), type_node->value_size(),
                                    protocol.c_str(), strlen(protocol.c_str())))
                        {
                            for (auto asurl_node = protocol_node->first_node();
                                 asurl_node;
                                 asurl_node = asurl_node->next_sibling())
                            {
                                if (compare(asurl_node->local_name(),
                                            asurl_node->local_name_size(),
                                            "ASUrl", strlen("ASUrl")))
                                {
                                    if (i >= 1)
                                    {
                                        result.internal_ews_url = std::string(
                                            asurl_node->value(),
                                            asurl_node->value_size());
                                        return result;
                                    }
                                    else
                                    {
                                        result.external_ews_url = std::string(
                                            asurl_node->value(),
                                            asurl_node->value_size());
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // If we reach this point, then either autodiscovery returned an
        // error or there is a redirect address to retry the autodiscover
        // lookup

        for (auto redirect_node = account_node->first_node(); redirect_node;
             redirect_node = redirect_node->next_sibling())
        {
            if (compare(redirect_node->local_name(),
                        redirect_node->local_name_size(), "RedirectAddr",
                        strlen("RedirectAddr")))
            {
                // Retry
                redirections++;
                const auto redirect_address = std::string(
                    redirect_node->value(), redirect_node->value_size());
                return get_exchange_web_services_url<RequestHandler>(
                    redirect_address, credentials, redirections, hints);
            }
        }

        throw exception("Autodiscovery failed unexpectedly");
    }

    inline std::string escape(const char* str)
    {
        std::string res;
        rapidxml::internal::copy_and_expand_chars(str, str + strlen(str), '\0',
                                                  std::back_inserter(res));
        return res;
    }

    inline std::string escape(const std::string& str)
    {
        return escape(str.c_str());
    }
} // namespace internal

//! Set-up EWS library.
//!
//! Should be called when application is still in single-threaded context.
//! Calling this function more than once does no harm.
//!
//! Note: Function is not thread-safe
inline void set_up() EWS_NOEXCEPT { curl_global_init(CURL_GLOBAL_DEFAULT); }

//! Clean-up EWS library.
//!
//! You should call this function only when no other thread is running.
//! See libcurl(3) man-page or http://curl.haxx.se/libcurl/c/libcurl.html
//!
//! Note: Function is not thread-safe
inline void tear_down() EWS_NOEXCEPT { curl_global_cleanup(); }

//! \brief Returns the EWS URL by querying the Autodiscover service.
//!
//! \param user_smtp_address User's primary SMTP address
//! \param credentials The user's credentials
//! \return The Exchange Web Services URLs as autodiscover_result properties
#ifdef EWS_HAS_DEFAULT_TEMPLATE_ARGS_FOR_FUNCTIONS
template <typename RequestHandler = internal::http_request>
#else
template <typename RequestHandler>
#endif
inline autodiscover_result
get_exchange_web_services_url(const std::string& user_smtp_address,
                              const basic_credentials& credentials)
{
    autodiscover_hints hints;
    return internal::get_exchange_web_services_url<RequestHandler>(
        user_smtp_address, credentials, 0U, hints);
}

//! \brief Returns the EWS URL by querying the Autodiscover service.
//!
//! \param user_smtp_address User's primary SMTP address
//! \param credentials The user's credentials
//! \param hints The url given by the user
//! \return The Exchange Web Services URLs as autodiscover_result properties
#ifdef EWS_HAS_DEFAULT_TEMPLATE_ARGS_FOR_FUNCTIONS
template <typename RequestHandler = internal::http_request>
#else
template <typename RequestHandler>
#endif
inline autodiscover_result
get_exchange_web_services_url(const std::string& user_smtp_address,
                              const basic_credentials& credentials,
                              const autodiscover_hints& hints)
{
    return internal::get_exchange_web_services_url<RequestHandler>(
        user_smtp_address, credentials, 0U, hints);
}

//! \brief The unique identifier and change key of an item in the
//! Exchange store.
//!
//! The ID uniquely identifies a concrete item throughout the Exchange
//! store and is not expected to change as long as the item exists. The
//! change key on the other hand identifies a specific version of an item.
//! It is expected to be changed whenever a property of the item is
//! changed. The change key is used for synchronization purposes on the
//! server. You only need to take care that the change key you include in a
//! service call is the most current one.
//!
//! Instances of this class are somewhat immutable. You can default
//! construct an item_id in which case valid() will always return false.
//! (Default construction is needed because we need item and it's
//! sub-classes to be default constructible.) Only item_ids that come from
//! an Exchange store are considered to be valid.
class item_id final
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    //! Constructs an invalid item_id instance
    item_id() = default;
#else
    item_id() {}
#endif

    //! Constructs an <tt>\<ItemId></tt> from given \p id string.
    explicit item_id(std::string id) : id_(std::move(id)), change_key_() {}

    //! \brief Constructs an <tt>\<ItemId></tt> from given identifier and
    //! change key.
    item_id(std::string id, std::string change_key)
        : id_(std::move(id)), change_key_(std::move(change_key))
    {
    }

    //! Returns the identifier.
    const std::string& id() const EWS_NOEXCEPT { return id_; }

    //! Returns the change key
    const std::string& change_key() const EWS_NOEXCEPT { return change_key_; }

    //! Whether this item_id is expected to be valid
    bool valid() const EWS_NOEXCEPT { return !id_.empty(); }

    //! Serializes this item_id to an XML string
    std::string to_xml() const
    {
        std::stringstream sstr;
        sstr << "<t:ItemId Id=\"" << id_;
        if (!change_key_.empty()) {
            sstr << "\" ChangeKey=\"" << change_key_;
        }
        sstr << "\"/>";
        return sstr.str();
    }

    //! Makes an item_id instance from an <tt>\<ItemId></tt> XML element
    static item_id from_xml_element(const rapidxml::xml_node<>& elem)
    {
        auto id_attr = elem.first_attribute("Id");
        check(id_attr, "Missing attribute Id in <ItemId>");
        auto id = std::string(id_attr->value(), id_attr->value_size());
        auto ckey_attr = elem.first_attribute("ChangeKey");
        check(ckey_attr, "Missing attribute ChangeKey in <ItemId>");
        auto ckey = std::string(ckey_attr->value(), ckey_attr->value_size());
        return item_id(std::move(id), std::move(ckey));
    }

private:
    // case-sensitive; therefore, comparisons between IDs must be
    // case-sensitive or binary
    std::string id_;
    std::string change_key_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<item_id>::value);
static_assert(std::is_copy_constructible<item_id>::value);
static_assert(std::is_copy_assignable<item_id>::value);
static_assert(std::is_move_constructible<item_id>::value);
static_assert(std::is_move_assignable<item_id>::value);
#endif

//! \brief The OccurrenceItemId element identifies a single occurrence of a
//! recurring item.
class occurrence_item_id final
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    //! Constructs an invalid item_id instance
    occurrence_item_id() = default;
#else
    occurrence_item_id() {}
#endif

    //! Constructs an <tt>\<OccurrenceItemId></tt> from given \p id string.
    occurrence_item_id(std::string id) // intentionally not explicit
        : id_(std::move(id)), change_key_(), instance_index_(1)
    {
    }

    //! \brief Constructs an <tt>\<OccurrenceItemId></tt> from a given
    //! item_id instance
    occurrence_item_id(const item_id& item_id) // intentionally not explicit
        : id_(item_id.id()), change_key_(item_id.change_key()),
          instance_index_(1)
    {
    }

    //! \brief Constructs an <tt>\<OccurrenceItemId></tt> from a given
    //! item_id instance
    occurrence_item_id(const item_id& item_id, int instance_index)
        : id_(item_id.id()), change_key_(item_id.change_key()),
          instance_index_(instance_index)
    {
    }

    //! \brief Constructs an <tt>\<OccurrenceItemId></tt> from given identifier
    //! and change key.
    occurrence_item_id(std::string id, std::string change_key,
                       int instance_index)
        : id_(std::move(id)), change_key_(std::move(change_key)),
          instance_index_(instance_index)
    {
    }

    //! Returns the identifier.
    const std::string& id() const EWS_NOEXCEPT { return id_; }

    //! Returns the change key
    const std::string& change_key() const EWS_NOEXCEPT { return change_key_; }

    //! Returns the instance index
    int instance_index() const EWS_NOEXCEPT { return instance_index_; }

    //! Whether this item_id is expected to be valid
    bool valid() const EWS_NOEXCEPT { return !id_.empty(); }

    //! Serializes this occurrence_item_id to an XML string
    std::string to_xml() const
    {
        std::stringstream sstr;
        sstr << "<t:OccurrenceItemId RecurringMasterId=\"" << id();
        sstr << "\" ChangeKey=\"" << change_key() << "\" InstanceIndex=\"";
        sstr << instance_index() << "\"/>";
        return sstr.str();
    }

    //! Makes an occurrence_item_id instance from an
    //! <tt>\<OccurrenceItemId></tt> XML element
    static occurrence_item_id from_xml_element(const rapidxml::xml_node<>& elem)
    {
        auto id_attr = elem.first_attribute("RecurringMasterId");
        check(id_attr,
              "Missing attribute RecurringMasterId in <OccurrenceItemId>");
        auto id = std::string(id_attr->value(), id_attr->value_size());

        auto ckey_attr = elem.first_attribute("ChangeKey");
        check(ckey_attr, "Missing attribute ChangeKey in <OccurrenceItemId>");
        auto ckey = std::string(ckey_attr->value(), ckey_attr->value_size());

        auto index_attr = elem.first_attribute("InstanceIndex");
        check(index_attr,
              "Missing attribute InstanceIndex in <OccurrenceItemId>");
        auto index = std::stoi(
            std::string(index_attr->value(), index_attr->value_size()));

        return occurrence_item_id(std::move(id), std::move(ckey), index);
    }

private:
    // case-sensitive; therefore, comparisons between IDs must be
    // case-sensitive or binary
    std::string id_;
    std::string change_key_;
    int instance_index_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<occurrence_item_id>::value);
static_assert(std::is_copy_constructible<occurrence_item_id>::value);
static_assert(std::is_copy_assignable<occurrence_item_id>::value);
static_assert(std::is_move_constructible<occurrence_item_id>::value);
static_assert(std::is_move_assignable<occurrence_item_id>::value);
#endif

//! \brief Contains the unique identifier of an attachment.
//!
//! The AttachmentId element identifies an item or file attachment. This
//! element is used in CreateAttachment responses.
class attachment_id final
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    attachment_id() = default;
#else
    attachment_id() {}
#endif

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
    explicit attachment_id(std::string id) : id_(std::move(id)) {}

    attachment_id(std::string id, item_id root_item_id)
        : id_(std::move(id)), root_item_id_(std::move(root_item_id))
    {
    }
#endif

    //! Returns the string representing the unique identifier of an
    //! attachment
    const std::string& id() const EWS_NOEXCEPT { return id_; }

    //! \brief Returns the item_id of the <em>parent</em> or <em>root</em>
    //! item.
    //!
    //! The root item is the item that contains the attachment.
    //!
    //! \note: the returned item_id is only valid and meaningful when you
    //! obtained this attachment_id in a call to \ref
    //! service::create_attachment.
    const item_id& root_item_id() const EWS_NOEXCEPT { return root_item_id_; }

    //! Whether this attachment_id is valid
    bool valid() const EWS_NOEXCEPT { return !id_.empty(); }

    std::string to_xml() const
    {
        std::stringstream sstr;
        sstr << "<t:AttachmentId Id=\"" << id() << "\"";
        if (root_item_id().valid())
        {
            sstr << " RootItemId=\"" << root_item_id().id()
                 << "\" RootItemChangeKey=\"" << root_item_id().change_key()
                 << "\"";
        }
        sstr << "/>";
        return sstr.str();
    }

    //! Makes an attachment_id instance from an \<AttachmentId> element
    static attachment_id from_xml_element(const rapidxml::xml_node<>& elem)
    {
        auto id_attr = elem.first_attribute("Id");
        check(id_attr, "Missing attribute Id in <AttachmentId>");
        auto id = std::string(id_attr->value(), id_attr->value_size());
        auto root_item_id = std::string();
        auto root_item_ckey = std::string();

        auto root_item_id_attr = elem.first_attribute("RootItemId");
        if (root_item_id_attr)
        {
            root_item_id = std::string(root_item_id_attr->value(),
                                       root_item_id_attr->value_size());
            auto root_item_ckey_attr =
                elem.first_attribute("RootItemChangeKey");
            check(root_item_ckey_attr,
                  "Expected attribute 'RootItemChangeKey'");
            root_item_ckey = std::string(root_item_ckey_attr->value(),
                                         root_item_ckey_attr->value_size());
        }

        return root_item_id.empty()
                   ? attachment_id(std::move(id))
                   : attachment_id(std::move(id),
                                   item_id(std::move(root_item_id),
                                           std::move(root_item_ckey)));
    }

private:
    std::string id_;
    item_id root_item_id_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<attachment_id>::value);
static_assert(std::is_copy_constructible<attachment_id>::value);
static_assert(std::is_copy_assignable<attachment_id>::value);
static_assert(std::is_move_constructible<attachment_id>::value);
static_assert(std::is_move_assignable<attachment_id>::value);
#endif

//! \brief Represents a SMTP mailbox.
//!
//! Identifies a fully resolved email address. Usually represents a
//! contact's email address, a message recipient, or the organizer of a
//! meeting.
class mailbox final
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    //! \brief Creates a new undefined mailbox.
    //!
    //! Only useful as return value to indicate that no mailbox is set or
    //! available. (Good candidate for std::optional)
    mailbox() = default;
#else
    mailbox() {}
#endif

    explicit mailbox(item_id id)
        : id_(std::move(id)), value_(), name_(), routing_type_(),
          mailbox_type_()
    {
    }

    explicit mailbox(std::string value, std::string name = std::string(),
                     std::string routing_type = std::string(),
                     std::string mailbox_type = std::string())
        : id_(), value_(std::move(value)), name_(std::move(name)),
          routing_type_(std::move(routing_type)),
          mailbox_type_(std::move(mailbox_type))
    {
    }

    //! True if this mailbox is undefined
    bool none() const EWS_NOEXCEPT { return value_.empty() && !id_.valid(); }

    // TODO: rename
    const item_id& id() const EWS_NOEXCEPT { return id_; }

    //! Returns the email address
    const std::string& value() const EWS_NOEXCEPT { return value_; }

    //! \brief Returns the name of the mailbox user.
    //!
    //! This attribute is optional.
    const std::string& name() const EWS_NOEXCEPT { return name_; }

    //! \brief Returns the routing type.
    //
    //! This attribute is optional. Default is SMTP
    const std::string& routing_type() const EWS_NOEXCEPT
    {
        return routing_type_;
    }

    //! \brief Returns the mailbox type.
    //!
    //! This attribute is optional.
    const std::string& mailbox_type() const EWS_NOEXCEPT
    {
        return mailbox_type_;
    }

    //! \brief Returns the XML serialized string of this mailbox.
    //!
    //! Note: \<Mailbox> is a part of
    //! http://schemas.microsoft.com/exchange/services/2006/types namespace.
    //! At least that is what the documentation says. However, in the
    //! \<GetDelegate> request the \<Mailbox> element is expected
    //! to be part of
    //! http://schemas.microsoft.com/exchange/services/2006/messages. This is
    //! the reason for the extra argument.
    std::string to_xml(const char* xmlns = "t") const
    {
        std::stringstream sstr;
        sstr << "<" << xmlns << ":Mailbox>";
        if (id().valid())
        {
            sstr << id().to_xml();
        }
        else
        {
            sstr << "<t:EmailAddress>" << value() << "</t:EmailAddress>";

            if (!name().empty())
            {
                sstr << "<t:Name>" << name() << "</t:Name>";
            }

            if (!routing_type().empty())
            {
                sstr << "<t:RoutingType>" << routing_type()
                     << "</t:RoutingType>";
            }

            if (!mailbox_type().empty())
            {
                sstr << "<t:MailboxType>" << mailbox_type()
                     << "</t:MailboxType>";
            }
        }
        sstr << "</" << xmlns << ":Mailbox>";
        return sstr.str();
    }

    //! \brief Creates a new \<Mailbox> XML element and appends it to given
    //! parent node.
    //!
    //! Returns a reference to the newly created element.
    rapidxml::xml_node<>& to_xml_element(rapidxml::xml_node<>& parent) const
    {
        auto doc = parent.document();

        check(doc, "Parent node needs to be somewhere in a document");

        using namespace internal;
        auto& mailbox_node = create_node(parent, "t:Mailbox");

        if (!id_.valid())
        {
            check(!value_.empty(),
                  "Neither item_id nor value set in mailbox instance");

            create_node(mailbox_node, "t:EmailAddress", value_);

            if (!name_.empty())
            {
                create_node(mailbox_node, "t:Name", name_);
            }

            if (!routing_type_.empty())
            {
                create_node(mailbox_node, "t:RoutingType", routing_type_);
            }

            if (!mailbox_type_.empty())
            {
                create_node(mailbox_node, "t:MailboxType", mailbox_type_);
            }
        }
        else
        {
            auto item_id_node = &create_node(mailbox_node, "t:ItemId");

            auto ptr_to_key = doc->allocate_string("Id");
            auto ptr_to_value = doc->allocate_string(id_.id().c_str());
            item_id_node->append_attribute(
                doc->allocate_attribute(ptr_to_key, ptr_to_value));

            ptr_to_key = doc->allocate_string("ChangeKey");
            ptr_to_value = doc->allocate_string(id_.change_key().c_str());
            item_id_node->append_attribute(
                doc->allocate_attribute(ptr_to_key, ptr_to_value));
        }
        return mailbox_node;
    }

    //! Makes a mailbox instance from a \<Mailbox> XML element
    static mailbox from_xml_element(const rapidxml::xml_node<>& elem)
    {
        using rapidxml::internal::compare;

        //  <Mailbox>
        //      <Name/>
        //      <EmailAddress/>
        //      <RoutingType/>
        //      <MailboxType/>
        //      <ItemId/>
        //  </Mailbox>

        std::string name;
        std::string address;
        std::string routing_type;
        std::string mailbox_type;
        item_id id;

        for (auto node = elem.first_node(); node; node = node->next_sibling())
        {
            if (compare(node->local_name(), node->local_name_size(), "Name",
                        strlen("Name")))
            {
                name = std::string(node->value(), node->value_size());
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "EmailAddress", strlen("EmailAddress")))
            {
                address = std::string(node->value(), node->value_size());
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "RoutingType", strlen("RoutingType")))
            {
                routing_type = std::string(node->value(), node->value_size());
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "MailboxType", strlen("MailboxType")))
            {
                mailbox_type = std::string(node->value(), node->value_size());
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "ItemId", strlen("ItemId")))
            {
                id = item_id::from_xml_element(*node);
            }
            else
            {
                throw exception("Unexpected child element in <Mailbox>");
            }
        }

        if (!id.valid())
        {
            return mailbox(std::move(address), std::move(name),
                           std::move(routing_type), std::move(mailbox_type));
        }

        return mailbox(std::move(id));
    }

private:
    item_id id_;
    std::string value_;
    std::string name_;
    std::string routing_type_;
    std::string mailbox_type_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<mailbox>::value);
static_assert(std::is_copy_constructible<mailbox>::value);
static_assert(std::is_copy_assignable<mailbox>::value);
static_assert(std::is_move_constructible<mailbox>::value);
static_assert(std::is_move_assignable<mailbox>::value);
#endif

class directory_id final
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    directory_id() = default;
#else
    directory_id() {}
#endif
    explicit directory_id(const std::string& str) : id_(str) {}
    const std::string& get_id() const EWS_NOEXCEPT { return id_; }

private:
    std::string id_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<directory_id>::value);
static_assert(std::is_copy_constructible<directory_id>::value);
static_assert(std::is_copy_assignable<directory_id>::value);
static_assert(std::is_move_constructible<directory_id>::value);
static_assert(std::is_move_assignable<directory_id>::value);
#endif

struct resolution final
{
    ews::mailbox mailbox;
    ews::directory_id directory_id;
    std::shared_ptr<ews::contact> contact;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<resolution>::value);
static_assert(std::is_copy_constructible<resolution>::value);
static_assert(std::is_copy_assignable<resolution>::value);
static_assert(std::is_move_constructible<resolution>::value);
static_assert(std::is_move_assignable<resolution>::value);
#endif

class resolution_set final
{
public:
    resolution_set()
        : includes_last_item_in_range(true), indexed_paging_offset(0),
          numerator_offset(0), absolute_denominator(0), total_items_in_view(0)
    {
    }

    //! Whether the resolution_set has no elements
    bool empty() const EWS_NOEXCEPT { return resolutions.empty(); }

    //! Range-based for loop support
    std::vector<resolution>::iterator begin() { return resolutions.begin(); }

    //! Range-based for loop support
    std::vector<resolution>::iterator end() { return resolutions.end(); }

    bool includes_last_item_in_range;
    int indexed_paging_offset;
    int numerator_offset;
    int absolute_denominator;
    int total_items_in_view;
    std::vector<resolution> resolutions;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<resolution_set>::value);
static_assert(std::is_copy_constructible<resolution_set>::value);
static_assert(std::is_copy_assignable<resolution_set>::value);
static_assert(std::is_move_constructible<resolution_set>::value);
static_assert(std::is_move_assignable<resolution_set>::value);
#endif

#ifdef EWS_HAS_VARIANT
//! Contains the information about the subscription
class subscription_information final
{
public:
#    ifdef EWS_HAS_DEFAULT_AND_DELETE
    subscription_information() = default;
#    else
    subscription_information() {}
#    endif
    subscription_information(std::string id, std::string mark)
        : subscription_id_(std::move(id)), watermark_(std::move(mark))
    {
    }

    const std::string& get_subscription_id() const EWS_NOEXCEPT
    {
        return subscription_id_;
    }
    const std::string& get_watermark() const EWS_NOEXCEPT { return watermark_; }

private:
    std::string subscription_id_;
    std::string watermark_;
};

#    if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                              \
        defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<subscription_information>::value);
static_assert(std::is_copy_constructible<subscription_information>::value);
static_assert(std::is_copy_assignable<subscription_information>::value);
static_assert(std::is_move_constructible<subscription_information>::value);
static_assert(std::is_move_assignable<subscription_information>::value);
#    endif
#endif

//! \brief Identifies a folder.
//!
//! Renders a <tt>\<FolderId></tt> element. Contains the identifier and
//! change key of a folder.
class folder_id
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    folder_id() = default;
    virtual ~folder_id() = default;
    folder_id(const folder_id&) = default;
#else
    folder_id() {}
    virtual ~folder_id() {}
#endif

    explicit folder_id(std::string id) : id_(std::move(id)), change_key_() {}

    folder_id(std::string id, std::string change_key)
        : id_(std::move(id)), change_key_(std::move(change_key))
    {
    }

    std::string to_xml() const { return this->to_xml_impl(); }

    //! Returns a string identifying a folder in the Exchange store
    const std::string& id() const EWS_NOEXCEPT { return id_; }

    //! Returns a string identifying a version of a folder
    const std::string& change_key() const EWS_NOEXCEPT { return change_key_; }

    //! Whether this folder_id is valid
    bool valid() const EWS_NOEXCEPT { return !id_.empty(); }

    //! Makes a folder_id instance from given XML element
    static folder_id from_xml_element(const rapidxml::xml_node<>& elem)
    {
        auto id_attr = elem.first_attribute("Id");
        check(id_attr, "Expected <Id> to have an Id attribute");
        auto id = std::string(id_attr->value(), id_attr->value_size());

        std::string change_key;
        auto ck_attr = elem.first_attribute("ChangeKey");
        if (ck_attr)
        {
            change_key = std::string(ck_attr->value(), ck_attr->value_size());
        }
        return folder_id(std::move(id), std::move(change_key));
    }

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
protected:
    virtual std::string to_xml_impl() const
    {
        std::stringstream sstr;
        sstr << "<t:"
             << "FolderId Id=\"" << id_;
        if (!change_key_.empty())
        {
            sstr << "\" ChangeKey=\"" << change_key_;
        }
        sstr << "\"/>";
        return sstr.str();
    }
#endif

private:
    std::string id_;
    std::string change_key_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<folder_id>::value);
static_assert(std::is_copy_constructible<folder_id>::value);
static_assert(std::is_copy_assignable<folder_id>::value);
static_assert(std::is_move_constructible<folder_id>::value);
static_assert(std::is_move_assignable<folder_id>::value);
#endif

//! \brief Renders a <tt>\<DistinguishedFolderId></tt> element.
//!
//! Implicitly convertible from \ref standard_folder.
class distinguished_folder_id final : public folder_id
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    distinguished_folder_id() = default;
#else
    distinguished_folder_id() : folder_id() {}
#endif

    //! \brief Creates a <tt>\<DistinguishedFolderId></tt> element for a given
    //! well-known folder.
    distinguished_folder_id(standard_folder folder) // Intentionally not
        : folder_id(well_known_name(folder))        // explicit
    {
    }

    //! \brief Creates a <tt>\<DistinguishedFolderId></tt> element for a given
    //! well-known folder and change key.
    distinguished_folder_id(standard_folder folder, std::string change_key)
        : folder_id(well_known_name(folder), std::move(change_key))
    {
    }

    //! \brief Constructor for EWS delegate access.
    //!
    //! Creates a <tt>\<DistinguishedFolderId></tt> element for a well-known
    //! folder of a different user. The user is the folder's owner.
    //!
    //! By specifying a well-known folder name and a SMTP mailbox address, a
    //! delegate can get access to the mailbox owner's folder and the items
    //! therein. If the resulting distinguished_folder_id is used in a
    //! subsequent find_item, get_{task,message,calendar_item,contact} call, the
    //! returned item_ids allow implicit access to the mailbox owner's items.
    //!
    //! This access pattern is described as explicit/implicit access in
    //! Microsoft's documentation.
    distinguished_folder_id(standard_folder folder, mailbox owner)
        : folder_id(well_known_name(folder)), owner_(std::move(owner))
    {
    }

    //! Returns the standard_folder enum for given string
    static standard_folder str_to_standard_folder(const std::string& name)
    {
        if (name == "calendar")
        {
            return standard_folder::calendar;
        }
        else if (name == "contacts")
        {
            return standard_folder::contacts;
        }
        else if (name == "deleteditems")
        {
            return standard_folder::deleted_items;
        }
        else if (name == "drafts")
        {
            return standard_folder::drafts;
        }
        else if (name == "inbox")
        {
            return standard_folder::inbox;
        }
        else if (name == "journal")
        {
            return standard_folder::journal;
        }
        else if (name == "notes")
        {
            return standard_folder::notes;
        }
        else if (name == "outbox")
        {
            return standard_folder::outbox;
        }
        else if (name == "sentitems")
        {
            return standard_folder::sent_items;
        }
        else if (name == "tasks")
        {
            return standard_folder::tasks;
        }
        else if (name == "msgfolderroot")
        {
            return standard_folder::msg_folder_root;
        }
        else if (name == "root")
        {
            return standard_folder::root;
        }
        else if (name == "junkemail")
        {
            return standard_folder::junk_email;
        }
        else if (name == "searchfolders")
        {
            return standard_folder::search_folders;
        }
        else if (name == "voicemail")
        {
            return standard_folder::voice_mail;
        }
        else if (name == "recoverableitemsroot")
        {
            return standard_folder::recoverable_items_root;
        }
        else if (name == "recoverableitemsdeletions")
        {
            return standard_folder::recoverable_items_deletions;
        }
        else if (name == "recoverableitemsversions")
        {
            return standard_folder::recoverable_items_versions;
        }
        else if (name == "recoverableitemspurges")
        {
            return standard_folder::recoverable_items_purges;
        }
        else if (name == "archiveroot")
        {
            return standard_folder::archive_root;
        }
        else if (name == "archivemsgfolderroot")
        {
            return standard_folder::archive_msg_folder_root;
        }
        else if (name == "archivedeleteditems")
        {
            return standard_folder::archive_deleted_items;
        }
        else if (name == "archiveinbox")
        {
            return standard_folder::archive_inbox;
        }
        else if (name == "archiverecoverableitemsroot")
        {
            return standard_folder::archive_recoverable_items_root;
        }
        else if (name == "archiverecoverableitemsdeletions")
        {
            return standard_folder::archive_recoverable_items_deletions;
        }
        else if (name == "archiverecoverableitemsversions")
        {
            return standard_folder::archive_recoverable_items_versions;
        }
        else if (name == "archiverecoverableitemspurges")
        {
            return standard_folder::archive_recoverable_items_purges;
        }
        else if (name == "syncissues")
        {
            return standard_folder::sync_issues;
        }
        else if (name == "conflicts")
        {
            return standard_folder::conflicts;
        }
        else if (name == "localfailures")
        {
            return standard_folder::local_failures;
        }
        else if (name == "serverfailures")
        {
            return standard_folder::server_failures;
        }
        else if (name == "recipientcache")
        {
            return standard_folder::recipient_cache;
        }
        else if (name == "quickcontacts")
        {
            return standard_folder::quick_contacts;
        }
        else if (name == "conversationhistory")
        {
            return standard_folder::conversation_history;
        }
        else if (name == "adminauditlogs")
        {
            return standard_folder::admin_audit_logs;
        }
        else if (name == "todosearch")
        {
            return standard_folder::todo_search;
        }
        else if (name == "mycontacts")
        {
            return standard_folder::my_contacts;
        }
        else if (name == "directory")
        {
            return standard_folder::directory;
        }
        else if (name == "imcontactlist")
        {
            return standard_folder::im_contact_list;
        }
        else if (name == "peopleconnect")
        {
            return standard_folder::people_connect;
        }
        else if (name == "favorites")
        {
            return standard_folder::favorites;
        }

        throw exception("Unrecognized folder name");
    }

    //! Returns the well-known name for given standard_folder as string.
    static std::string well_known_name(standard_folder enumeration)
    {
        std::string name;
        switch (enumeration)
        {
        case standard_folder::calendar:
            name = "calendar";
            break;

        case standard_folder::contacts:
            name = "contacts";
            break;

        case standard_folder::deleted_items:
            name = "deleteditems";
            break;

        case standard_folder::drafts:
            name = "drafts";
            break;

        case standard_folder::inbox:
            name = "inbox";
            break;

        case standard_folder::journal:
            name = "journal";
            break;

        case standard_folder::notes:
            name = "notes";
            break;

        case standard_folder::outbox:
            name = "outbox";
            break;

        case standard_folder::sent_items:
            name = "sentitems";
            break;

        case standard_folder::tasks:
            name = "tasks";
            break;

        case standard_folder::msg_folder_root:
            name = "msgfolderroot";
            break;

        case standard_folder::root:
            name = "root";
            break;

        case standard_folder::junk_email:
            name = "junkemail";
            break;

        case standard_folder::search_folders:
            name = "searchfolders";
            break;

        case standard_folder::voice_mail:
            name = "voicemail";
            break;

        case standard_folder::recoverable_items_root:
            name = "recoverableitemsroot";
            break;

        case standard_folder::recoverable_items_deletions:
            name = "recoverableitemsdeletions";
            break;

        case standard_folder::recoverable_items_versions:
            name = "recoverableitemsversions";
            break;

        case standard_folder::recoverable_items_purges:
            name = "recoverableitemspurges";
            break;

        case standard_folder::archive_root:
            name = "archiveroot";
            break;

        case standard_folder::archive_msg_folder_root:
            name = "archivemsgfolderroot";
            break;

        case standard_folder::archive_deleted_items:
            name = "archivedeleteditems";
            break;

        case standard_folder::archive_inbox:
            name = "archiveinbox";
            break;

        case standard_folder::archive_recoverable_items_root:
            name = "archiverecoverableitemsroot";
            break;

        case standard_folder::archive_recoverable_items_deletions:
            name = "archiverecoverableitemsdeletions";
            break;

        case standard_folder::archive_recoverable_items_versions:
            name = "archiverecoverableitemsversions";
            break;

        case standard_folder::archive_recoverable_items_purges:
            name = "archiverecoverableitemspurges";
            break;

        case standard_folder::sync_issues:
            name = "syncissues";
            break;

        case standard_folder::conflicts:
            name = "conflicts";
            break;

        case standard_folder::local_failures:
            name = "localfailures";
            break;

        case standard_folder::server_failures:
            name = "serverfailures";
            break;

        case standard_folder::recipient_cache:
            name = "recipientcache";
            break;

        case standard_folder::quick_contacts:
            name = "quickcontacts";
            break;

        case standard_folder::conversation_history:
            name = "conversationhistory";
            break;

        case standard_folder::admin_audit_logs:
            name = "adminauditlogs";
            break;

        case standard_folder::todo_search:
            name = "todosearch";
            break;

        case standard_folder::my_contacts:
            name = "mycontacts";
            break;

        case standard_folder::directory:
            name = "directory";
            break;

        case standard_folder::im_contact_list:
            name = "imcontactlist";
            break;

        case standard_folder::people_connect:
            name = "peopleconnect";
            break;

        case standard_folder::favorites:
            name = "favorites";
            break;

        default:
            throw exception("Unrecognized folder name");
        };
        return name;
    }

private:
#ifdef EWS_HAS_OPTIONAL
    std::optional<mailbox> owner_;
#else
    internal::optional<mailbox> owner_;
#endif

    std::string to_xml_impl() const override
    {
        std::stringstream sstr;
        sstr << "<t:DistinguishedFolderId Id=\"";
        sstr << id();

        if (owner_.has_value())
        {
            sstr << "\">";
            sstr << owner_.value().to_xml();
            sstr << "</t:DistinguishedFolderId>";
        }
        else
        {
            if (!change_key().empty())
            {
                sstr << "\" ChangeKey=\"" << change_key();
            }
            sstr << "\"/>";
        }
        return sstr.str();
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<distinguished_folder_id>::value);
static_assert(std::is_copy_constructible<distinguished_folder_id>::value);
static_assert(std::is_copy_assignable<distinguished_folder_id>::value);
static_assert(std::is_move_constructible<distinguished_folder_id>::value);
static_assert(std::is_move_assignable<distinguished_folder_id>::value);
#endif

#ifdef EWS_HAS_VARIANT
namespace internal
{
    class event_base
    {
    public:
        const event_type& get_type() const EWS_NOEXCEPT { return type_; }
        const std::string& get_watermark() const EWS_NOEXCEPT
        {
            return watermark_;
        }

    protected:
        event_type type_;
        std::string watermark_;
    };
} // namespace internal
//! Represents a <CopiedEvent>
class copied_event final : public internal::event_base
{
public:
#    ifdef EWS_HAS_DEFAULT_AND_DELETE
    copied_event() = default;
#    else
    copied_event() {}
#    endif

    static copied_event from_xml_element(const rapidxml::xml_node<>& elem)
    {
        using rapidxml::internal::compare;
        copied_event e;
        std::string watermark;
        std::string timestamp;
        item_id id;
        item_id old_id;
        folder_id f_id;
        folder_id old_f_id;
        folder_id parent_folder_id;
        folder_id old_parent_folder_id;
        for (auto node = elem.first_node(); node; node = node->next_sibling())
        {
            if (compare(node->local_name(), node->local_name_size(),
                        "Watermark", strlen("Watermark")))
            {
                watermark = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "TimeStamp", strlen("TimeStamp")))
            {
                timestamp = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "ParentFolderId", strlen("ParentFolderId")))
            {
                parent_folder_id = folder_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "OldParentFolderId", strlen("OldParentFolderId")))
            {
                old_parent_folder_id = folder_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(), "ItemId",
                        strlen("ItemId")))
            {
                id = item_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "OldItemId", strlen("OldItemId")))
            {
                old_id = item_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(), "FolderId",
                        strlen("FolderId")))
            {
                f_id = folder_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "OldFolderId", strlen("OldFolderId")))
            {
                old_f_id = folder_id::from_xml_element(*node);
            }
        }
        e.type_ = event_type::copied_event;
        e.watermark_ = watermark;
        e.timestamp_ = timestamp;
        e.id_ = id;
        e.old_item_id_ = old_id;
        e.folder_id_ = f_id;
        e.old_folder_id_ = old_f_id;
        e.parent_folder_id_ = parent_folder_id;
        e.old_parent_folder_id_ = old_parent_folder_id;

        return e;
    };

    const std::string& get_timestamp() const EWS_NOEXCEPT { return timestamp_; }
    const item_id& get_item_id() const EWS_NOEXCEPT { return id_; }
    const item_id& get_old_item_id() const EWS_NOEXCEPT { return old_item_id_; }
    const folder_id& get_folder_id() const EWS_NOEXCEPT { return folder_id_; }
    const folder_id& get_old_folder_id() const EWS_NOEXCEPT
    {
        return old_folder_id_;
    }
    const folder_id& get_parent_folder_id() const EWS_NOEXCEPT
    {
        return parent_folder_id_;
    }
    const folder_id& get_old_parent_folder_id() const EWS_NOEXCEPT
    {
        return old_parent_folder_id_;
    }

private:
    std::string timestamp_;
    item_id id_;
    item_id old_item_id_;
    folder_id folder_id_;
    folder_id old_folder_id_;
    folder_id parent_folder_id_;
    folder_id old_parent_folder_id_;
};

//! Represents a <CreatedEvent>
class created_event final : public internal::event_base
{
public:
#    ifdef EWS_HAS_DEFAULT_AND_DELETE
    created_event() = default;
#    else
    created_event() {}
#    endif
    static created_event from_xml_element(const rapidxml::xml_node<>& elem)
    {
        using rapidxml::internal::compare;
        created_event e;
        std::string watermark;
        std::string timestamp;
        item_id id;
        folder_id f_id;
        folder_id parent_folder_id;
        for (auto node = elem.first_node(); node; node = node->next_sibling())
        {
            if (compare(node->local_name(), node->local_name_size(),
                        "Watermark", strlen("Watermark")))
            {
                watermark = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "TimeStamp", strlen("TimeStamp")))
            {
                timestamp = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "ParentFolderId", strlen("ParentFolderId")))
            {
                parent_folder_id = folder_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(), "ItemId",
                        strlen("ItemId")))
            {
                id = item_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(), "FolderId",
                        strlen("FolderId")))
            {
                f_id = folder_id::from_xml_element(*node);
            }
        }
        e.type_ = event_type::created_event;
        e.watermark_ = watermark;
        e.timestamp_ = timestamp;
        e.id_ = id;
        e.folder_id_ = f_id;
        e.parent_folder_id_ = parent_folder_id;
        return e;
    };

    const std::string& get_timestamp() const EWS_NOEXCEPT { return timestamp_; }
    const item_id& get_item_id() const EWS_NOEXCEPT { return id_; }
    const folder_id& get_folder_id() const EWS_NOEXCEPT { return folder_id_; }
    const folder_id& get_parent_folder_id() const EWS_NOEXCEPT
    {
        return parent_folder_id_;
    }

private:
    std::string timestamp_;
    item_id id_;
    folder_id folder_id_;
    folder_id parent_folder_id_;
};

//! Represents a <DeletedEvent>
class deleted_event final : public internal::event_base
{
public:
#    ifdef EWS_HAS_DEFAULT_AND_DELETE
    deleted_event() = default;
#    else
    deleted_event() {}
#    endif

    static deleted_event from_xml_element(const rapidxml::xml_node<>& elem)
    {
        using rapidxml::internal::compare;
        deleted_event e;
        std::string watermark;
        std::string timestamp;
        item_id id;
        folder_id f_id;
        folder_id parent_folder_id;
        for (auto node = elem.first_node(); node; node = node->next_sibling())
        {
            if (compare(node->local_name(), node->local_name_size(),
                        "Watermark", strlen("Watermark")))
            {
                watermark = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "TimeStamp", strlen("TimeStamp")))
            {
                timestamp = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "ParentFolderId", strlen("ParentFolderId")))
            {
                parent_folder_id = folder_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(), "ItemId",
                        strlen("ItemId")))
            {
                id = item_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(), "FolderId",
                        strlen("FolderId")))
            {
                f_id = folder_id::from_xml_element(*node);
            }
        }
        e.type_ = event_type::deleted_event;
        e.watermark_ = watermark;
        e.timestamp_ = timestamp;
        e.id_ = id;
        e.folder_id_ = f_id;
        e.parent_folder_id_ = parent_folder_id;

        return e;
    };

    const std::string& get_timestamp() const EWS_NOEXCEPT { return timestamp_; }
    const item_id& get_item_id() const EWS_NOEXCEPT { return id_; }
    const folder_id& get_folder_id() const EWS_NOEXCEPT { return folder_id_; }
    const folder_id& get_parent_folder_id() const EWS_NOEXCEPT
    {
        return parent_folder_id_;
    }

private:
    std::string timestamp_;
    item_id id_;
    folder_id folder_id_;
    folder_id parent_folder_id_;
};

//! Represents a <ModifiedEvent>
class modified_event final : public internal::event_base
{
public:
    modified_event() : unread_count_(0) {}

    static modified_event from_xml_element(const rapidxml::xml_node<>& elem)
    {
        using rapidxml::internal::compare;
        modified_event e;
        std::string watermark;
        std::string timestamp;
        item_id id;
        folder_id f_id;
        folder_id parent_folder_id;
        int unread_count = 0;
        for (auto node = elem.first_node(); node; node = node->next_sibling())
        {
            if (compare(node->local_name(), node->local_name_size(),
                        "Watermark", strlen("Watermark")))
            {
                watermark = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "TimeStamp", strlen("TimeStamp")))
            {
                timestamp = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "ParentFolderId", strlen("ParentFolderId")))
            {
                parent_folder_id = folder_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "UnreadCount", strlen("UnreadCount")))
            {
                unread_count = int(*node->value());
            }
            if (compare(node->local_name(), node->local_name_size(), "ItemId",
                        strlen("ItemId")))
            {
                id = item_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(), "FolderId",
                        strlen("FolderId")))
            {
                f_id = folder_id::from_xml_element(*node);
            }
        }
        e.type_ = event_type::modified_event;
        e.watermark_ = watermark;
        e.timestamp_ = timestamp;
        e.id_ = id;
        e.folder_id_ = f_id;
        e.parent_folder_id_ = parent_folder_id;
        e.unread_count_ = unread_count;

        return e;
    };

    const std::string& get_timestamp() const EWS_NOEXCEPT { return timestamp_; }
    const item_id& get_item_id() const EWS_NOEXCEPT { return id_; }
    const folder_id& get_folder_id() const EWS_NOEXCEPT { return folder_id_; }
    const folder_id& get_parent_folder_id() const EWS_NOEXCEPT
    {
        return parent_folder_id_;
    }
    const int& get_unread_count() const EWS_NOEXCEPT { return unread_count_; }

private:
    std::string timestamp_;
    int unread_count_;
    item_id id_;
    folder_id folder_id_;
    folder_id parent_folder_id_;
};

//! Represents a <MovedEvent>
class moved_event final : public internal::event_base
{
public:
#    ifdef EWS_HAS_DEFAULT_AND_DELETE
    moved_event() = default;
#    else
    moved_event() {}
#    endif

    static moved_event from_xml_element(const rapidxml::xml_node<>& elem)
    {
        using rapidxml::internal::compare;
        moved_event e;
        std::string watermark;
        std::string timestamp;
        item_id id;
        item_id old_id;
        folder_id f_id;
        folder_id old_f_id;
        folder_id parent_folder_id;
        folder_id old_parent_folder_id;
        for (auto node = elem.first_node(); node; node = node->next_sibling())
        {
            if (compare(node->local_name(), node->local_name_size(),
                        "Watermark", strlen("Watermark")))
            {
                watermark = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "TimeStamp", strlen("TimeStamp")))
            {
                timestamp = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "ParentFolderId", strlen("ParentFolderId")))
            {
                parent_folder_id = folder_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(), "ItemId",
                        strlen("ItemId")))
            {
                id = item_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "OldItemId", strlen("OldItemId")))
            {
                old_id = item_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(), "FolderId",
                        strlen("FolderId")))
            {
                f_id = folder_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "OldFolderId", strlen("OldFolderId")))
            {
                old_f_id = folder_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "OldParentFolderId", strlen("OldParentFolderId")))
            {
                old_parent_folder_id = folder_id::from_xml_element(*node);
            }
        }
        e.type_ = event_type::moved_event;
        e.watermark_ = watermark;
        e.timestamp_ = timestamp;
        e.id_ = id;
        e.old_item_id_ = old_id;
        e.folder_id_ = f_id;
        e.old_folder_id_ = old_f_id;
        e.parent_folder_id_ = parent_folder_id;
        e.old_parent_folder_id_ = old_parent_folder_id;

        return e;
    };

    const std::string& get_timestamp() const EWS_NOEXCEPT { return timestamp_; }
    const item_id& get_item_id() const EWS_NOEXCEPT { return id_; }
    const item_id& get_old_item_id() const EWS_NOEXCEPT { return old_item_id_; }
    const folder_id& get_folder_id() const EWS_NOEXCEPT { return folder_id_; }
    const folder_id& get_old_folder_id() const EWS_NOEXCEPT
    {
        return old_folder_id_;
    }
    const folder_id& get_parent_folder_id() const EWS_NOEXCEPT
    {
        return parent_folder_id_;
    }
    const folder_id& get_old_parent_folder_id() const EWS_NOEXCEPT
    {
        return old_parent_folder_id_;
    }

private:
    std::string timestamp_;
    item_id id_;
    item_id old_item_id_;
    folder_id folder_id_;
    folder_id old_folder_id_;
    folder_id parent_folder_id_;
    folder_id old_parent_folder_id_;
};

//! Represents a <NewMailEvent>
class new_mail_event final : public internal::event_base
{
public:
#    ifdef EWS_HAS_DEFAULT_AND_DELETE
    new_mail_event() = default;
#    else
    new_mail_event() {}
#    endif

    static new_mail_event from_xml_element(const rapidxml::xml_node<>& elem)
    {
        using rapidxml::internal::compare;
        new_mail_event e;
        std::string watermark;
        std::string timestamp;
        item_id id;
        folder_id parent_folder_id;
        for (auto node = elem.first_node(); node; node = node->next_sibling())
        {
            if (compare(node->local_name(), node->local_name_size(),
                        "Watermark", strlen("Watermark")))
            {
                watermark = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "TimeStamp", strlen("TimeStamp")))
            {
                timestamp = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "ParentFolderId", strlen("ParentFolderId")))
            {
                parent_folder_id = folder_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(), "ItemId",
                        strlen("ItemId")))
            {
                id = item_id::from_xml_element(*node);
            }
        }
        e.type_ = event_type::new_mail_event;
        e.watermark_ = watermark;
        e.timestamp_ = timestamp;
        e.id_ = id;
        e.parent_folder_id_ = parent_folder_id;

        return e;
    };

    const std::string& get_timestamp() const EWS_NOEXCEPT { return timestamp_; }
    const item_id& get_item_id() const EWS_NOEXCEPT { return id_; }
    const folder_id& get_parent_folder_id() const EWS_NOEXCEPT
    {
        return parent_folder_id_;
    }

private:
    std::string timestamp_;
    item_id id_;
    folder_id parent_folder_id_;
};

//! Represents a <StatusEvent>
class status_event final : public internal::event_base
{
public:
#    ifdef EWS_HAS_DEFAULT_AND_DELETE
    status_event() = default;
#    else
    status_event() {}
#    endif
    static status_event from_xml_element(const rapidxml::xml_node<>& elem)
    {
        using rapidxml::internal::compare;
        status_event e;
        std::string watermark;
        for (auto node = elem.first_node(); node; node = node->next_sibling())
        {
            if (compare(node->local_name(), node->local_name_size(),
                        "Watermark", strlen("Watermark")))
            {
                watermark = std::string(node->value(), node->value_size());
            }
        }
        e.type_ = event_type::new_mail_event;
        e.watermark_ = watermark;
        return e;
    };
};

//! Represents a <FreeBusyChangedEvent>
class free_busy_changed_event final : public internal::event_base
{
public:
#    ifdef EWS_HAS_DEFAULT_AND_DELETE
    free_busy_changed_event() = default;
#    else
    free_busy_changed_event() {}
#    endif

    static free_busy_changed_event
    from_xml_element(const rapidxml::xml_node<>& elem)
    {
        using rapidxml::internal::compare;
        free_busy_changed_event e;
        std::string watermark;
        std::string timestamp;
        item_id id;
        folder_id parent_folder_id;
        for (auto node = elem.first_node(); node; node = node->next_sibling())
        {
            if (compare(node->local_name(), node->local_name_size(),
                        "Watermark", strlen("Watermark")))
            {
                watermark = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "TimeStamp", strlen("TimeStamp")))
            {
                timestamp = std::string(node->value(), node->value_size());
            }
            if (compare(node->local_name(), node->local_name_size(),
                        "ParentFolderId", strlen("ParentFolderId")))
            {
                parent_folder_id = folder_id::from_xml_element(*node);
            }
            if (compare(node->local_name(), node->local_name_size(), "ItemId",
                        strlen("ItemId")))
            {
                id = item_id::from_xml_element(*node);
            }
        }
        e.type_ = event_type::free_busy_changed_event;
        e.watermark_ = watermark;
        e.timestamp_ = timestamp;
        e.id_ = id;
        e.parent_folder_id_ = parent_folder_id;

        return e;
    };

    const std::string& get_timestamp() const EWS_NOEXCEPT { return timestamp_; }
    const item_id& get_item_id() const EWS_NOEXCEPT { return id_; }
    const folder_id& get_parent_folder_id() const EWS_NOEXCEPT
    {
        return parent_folder_id_;
    }

private:
    std::string timestamp_;
    item_id id_;
    folder_id parent_folder_id_;
};

//! Contains all events that can be returned from get_events
typedef std::variant<copied_event, created_event, deleted_event, modified_event,
                     moved_event, new_mail_event, status_event,
                     free_busy_changed_event>
    event;

//! Represents a <Notification>
struct notification final
{
    std::string subscription_id;
    std::string previous_watermark;
    bool more_events;
    std::vector<event> events;
};
#endif

//! Represents a <tt>\<FileAttachment></tt> or an <tt>\<ItemAttachment></tt>
class attachment final
{
public:
    // Note that we are careful to avoid use of get_element_by_qname() here
    // because it might return elements of the enclosed item and not what
    // we'd expect

    //! Describes whether an attachment contains a file or another item.
    enum class type
    {
        //! An <tt>\<ItemAttachment></tt>
        item,

        //! A <tt>\<FileAttachment></tt>
        file
    };

    attachment() : xml_(), type_(type::item) {}

    //! Returns this attachment's attachment_id
    attachment_id id() const
    {
        const auto node = get_node("AttachmentId");
        return node ? attachment_id::from_xml_element(*node) : attachment_id();
    }

    //! Returns the attachment's name
    std::string name() const
    {
        const auto node = get_node("Name");
        return node ? std::string(node->value(), node->value_size()) : "";
    }

    //! \brief Returns the attachment's mime content
    //!
    //! If this is a <tt>\<ItemAttachment></tt> and the mime content was
    //! requested, returns the Base64-encoded mime content of the attachment. If
    //! this is an \<FileAttachment> or the mime content was not requested,
    //! returns an empty string.
    std::string get_mime_content() const
    {
        using rapidxml::internal::compare;

        const auto attachment_node = xml_.root();
        if (!attachment_node)
        {
            return "";
        }

        for (auto child = attachment_node->first_node(); child != nullptr;
             child = child->next_sibling())
        {
            for (auto inner = child->first_node(); inner != nullptr;
                 inner = inner->next_sibling())
            {
                if (compare(inner->local_name(), inner->local_name_size(),
                            "MimeContent", strlen("MimeContent")))
                {
                    return std::string(inner->value(), inner->value_size());
                }
            }
        }
        return "";
    }

    //! Returns the attachment's content type
    std::string content_type() const
    {
        const auto node = get_node("ContentType");
        return node ? std::string(node->value(), node->value_size()) : "";
    }

    //! \brief Returns the attachment's contents
    //!
    //! If this is a <tt>\<FileAttachment></tt>, returns the Base64-encoded
    //! contents of the file attachment. If this is an \<ItemAttachment>,
    //! the empty string.
    std::string content() const
    {
        const auto node = get_node("Content");
        return node ? std::string(node->value(), node->value_size()) : "";
    }

    //! \brief Returns the attachment's size in bytes
    //!
    //! If this is a <tt>\<FileAttachment></tt>, returns the size in bytes
    //! of the file attachment; otherwise 0.
    size_t content_size() const
    {
        const auto node = get_node("Size");
        if (!node)
        {
            return 0U;
        }
        auto size = std::string(node->value(), node->value_size());
        return size.empty() ? 0U : std::stoul(size);
    }

    //! \brief Returns the attachement's content ID
    //!
    //! If this is an inlined attachment, returns the content ID that is used
    //! to reference the attachment in the HTML code of the parent item.
    //! Otherwise returns an empty string.
    std::string content_id() const
    {
        const auto node = get_node("ContentId");
        return node ? std::string(node->value(), node->value_size()) : "";
    }

    //! Returns true if the attachment is inlined
    bool is_inline() const
    {
        const auto node = get_node("IsInline");
        if (!node)
        {
            return false;
        }
        using rapidxml::internal::compare;
        return compare(node->value(), node->value_size(), "true",
                       strlen("true"));
    }

    //! Returns either type::file or type::item
    type get_type() const EWS_NOEXCEPT { return type_; }

    //! \brief Write contents to a file
    //!
    //! If this is a <tt>\<FileAttachment></tt>, writes content to file.
    //! Does nothing if this is an <tt>\<ItemAttachment></tt>.  Returns the
    //! number of bytes written.
    size_t write_content_to_file(const std::string& file_path) const
    {
        if (get_type() == type::item)
        {
            return 0U;
        }

        const auto raw_bytes = internal::base64::decode(content());

        std::ofstream ofstr(file_path, std::ofstream::out | std::ios::binary);
        if (!ofstr.is_open())
        {
            if (file_path.empty())
            {
                throw exception(
                    "Could not open file for writing: no file name given");
            }

            throw exception("Could not open file for writing: " + file_path);
        }

        std::copy(begin(raw_bytes), end(raw_bytes),
                  std::ostreambuf_iterator<char>(ofstr));
        ofstr.close();
        return raw_bytes.size();
    }

    //! Returns this attachment serialized to XML
    std::string to_xml() const { return xml_.to_string(); }

    //! Constructs an attachment from a given XML element \p elem.
    static attachment from_xml_element(const rapidxml::xml_node<>& elem)
    {
        const auto elem_name =
            std::string(elem.local_name(), elem.local_name_size());
        check((elem_name == "FileAttachment" || elem_name == "ItemAttachment"),
              "Expected <FileAttachment> or <ItemAttachment>");
        return attachment(elem_name == "FileAttachment" ? type::file
                                                        : type::item,
                          internal::xml_subtree(elem));
    }

    //! \brief Creates a new <tt>\<FileAttachment></tt> from a given base64
    //! string.
    //!
    //! Returns a new <tt>\<FileAttachment></tt> that you can pass to
    //! ews::service::create_attachment in order to create the attachment on
    //! the server.
    //!
    //! \param content Base64 content of a file
    //! \param content_type The (RFC 2046) MIME content type of the
    //!        attachment
    //! \param name A name for this attachment
    //!
    //! On Windows you can use HKEY_CLASSES_ROOT/MIME/Database/Content Type
    //! registry hive to get the content type from a file extension. On a
    //! UNIX see magic(5) and file(1).
    static attachment from_base64(const std::string& content,
                                  std::string content_type, std::string name)
    {
        using internal::create_node;

        auto obj = attachment();
        obj.type_ = type::file;

        auto& attachment_node =
            create_node(*obj.xml_.document(), "t:FileAttachment");
        create_node(attachment_node, "t:Name", name);
        create_node(attachment_node, "t:ContentType", content_type);
        create_node(attachment_node, "t:Content", content);
        create_node(attachment_node, "t:Size", std::to_string(content.size()));

        return obj;
    }

    //! \brief Creates a new <tt>\<FileAttachment></tt> from a given file.
    //!
    //! Returns a new <tt>\<FileAttachment></tt> that you can pass to
    //! ews::service::create_attachment in order to create the attachment on
    //! the server.
    //!
    //! \param file_path Path to an existing and readable file
    //! \param content_type The (RFC 2046) MIME content type of the
    //!        attachment
    //! \param name A name for this attachment
    //!
    //! On Windows you can use HKEY_CLASSES_ROOT/MIME/Database/Content Type
    //! registry hive to get the content type from a file extension. On a
    //! UNIX see magic(5) and file(1).
    static attachment from_file(const std::string& file_path,
                                std::string content_type, std::string name)
    {
        using internal::create_node;

        // Try open file
        std::ifstream ifstr(file_path, std::ifstream::in | std::ios::binary);
        if (!ifstr.is_open())
        {
            throw exception("Could not open file for reading: " + file_path);
        }

        // Stop eating newlines in binary mode
        ifstr.unsetf(std::ios::skipws);

        // Determine size
        ifstr.seekg(0, std::ios::end);
        const auto file_size = ifstr.tellg();
        ifstr.seekg(0, std::ios::beg);

        // Allocate buffer
        auto buffer = std::vector<unsigned char>();
        buffer.reserve(
            internal::numeric_cast<std::vector<unsigned char>::size_type>(
                file_size));

        // Read
        buffer.insert(begin(buffer),
                      std::istream_iterator<unsigned char>(ifstr),
                      std::istream_iterator<unsigned char>());
        ifstr.close();

        // And encode
        auto content = internal::base64::encode(buffer);

        auto obj = attachment();
        obj.type_ = type::file;

        auto& attachment_node =
            create_node(*obj.xml_.document(), "t:FileAttachment");
        create_node(attachment_node, "t:Name", name);
        create_node(attachment_node, "t:ContentType", content_type);
        create_node(attachment_node, "t:Content", content);
        create_node(attachment_node, "t:Size", std::to_string(buffer.size()));

        return obj;
    }

    static attachment from_item(const item& the_item,
                                const std::string& name); // implemented
                                                          // below

private:
    internal::xml_subtree xml_;
    type type_;

    attachment(type&& t, internal::xml_subtree&& xml)
        : xml_(std::move(xml)), type_(std::move(t))
    {
    }

    rapidxml::xml_node<>* get_node(const char* local_name) const
    {
        using rapidxml::internal::compare;

        const auto attachment_node = xml_.root();
        if (!attachment_node)
        {
            return nullptr;
        }

        rapidxml::xml_node<>* node = nullptr;
        for (auto child = attachment_node->first_node(); child != nullptr;
             child = child->next_sibling())
        {
            if (compare(child->local_name(), child->local_name_size(),
                        local_name, strlen(local_name)))
            {
                node = child;
                break;
            }
        }
        return node;
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<attachment>::value);
static_assert(std::is_copy_constructible<attachment>::value);
static_assert(std::is_copy_assignable<attachment>::value);
static_assert(std::is_move_constructible<attachment>::value);
static_assert(std::is_move_assignable<attachment>::value);
#endif

namespace internal
{
    // Parse response class, code, and message text from given response element.
    inline response_result
    parse_response_class_and_code(const rapidxml::xml_node<>& elem)
    {
        using rapidxml::internal::compare;

        auto cls = response_class::success;
        auto response_class_attr = elem.first_attribute("ResponseClass");
        if (compare(response_class_attr->value(),
                    response_class_attr->value_size(), "Error", 5))
        {
            cls = response_class::error;
        }
        else if (compare(response_class_attr->value(),
                         response_class_attr->value_size(), "Warning", 7))
        {
            cls = response_class::warning;
        }

        // One thing we can count on is that when the ResponseClass
        // attribute is set to Success, ResponseCode will be set to NoError.
        // So we only parse the <ResponseCode> element when we have a
        // warning or an error.

        auto code = response_code::no_error;
        if (cls != response_class::success)
        {
            auto response_code_elem = elem.first_node_ns(
                uri<>::microsoft::messages(), "ResponseCode");
            check(response_code_elem, "Expected <ResponseCode> element");
            code = str_to_response_code(response_code_elem->value());

            auto message_text_elem =
                elem.first_node_ns(uri<>::microsoft::messages(), "MessageText");
            if (message_text_elem)
            {
                return response_result(
                    cls, code,
                    std::string(message_text_elem->value(),
                                message_text_elem->value_size()));
            }
        }

        return response_result(cls, code);
    }

    // Iterate over <Items> array and execute given function for each node.
    //
    // items_elem: an <Items> element
    // func: A callable that is invoked for each item in the response
    // message's <Items> array. A const rapidxml::xml_node& is passed to
    // that callable.
    template <typename Func>
    inline void for_each_child_node(const rapidxml::xml_node<>& parent_node,
                                    Func func)
    {
        for (auto child = parent_node.first_node(); child;
             child = child->next_sibling())
        {
            func(*child);
        }
    }

    template <typename Func>
    inline void for_each_attribute(const rapidxml::xml_node<>& node, Func func)
    {
        for (auto attrib = node.first_attribute(); attrib;
             attrib = attrib->next_attribute())
        {
            func(*attrib);
        }
    }

    //! Utility function for checking response messages, if they
    //! are an error. Throws an exception in this case.
    //! \param element XML node containing the response message
    inline void
    check_response_message_for_error(const rapidxml::xml_node<>& element)
    {
        using rapidxml::internal::compare;

        auto response_class_attr = element.first_attribute("ResponseClass");

        if (compare(response_class_attr->value(),
                    response_class_attr->value_size(), "Error", 5))
        {
            auto code = response_code::no_error;

            auto rcode_elem = element.first_node_ns(
                uri<>::microsoft::messages(), "ResponseCode");
            check(rcode_elem, "Expected <ResponseCode> element");

            code = str_to_response_code(rcode_elem->value());
            auto message_text = element.first_node_ns(
                uri<>::microsoft::messages(), "MessageText");

            if (message_text)
            {
                throw exchange_error(code,
                                     std::string(message_text->value(),
                                                 message_text->value_size()));
            }

            throw exchange_error(code);
        }
    }

    // Base-class for all response messages
    class response_message_base
    {
    public:
        explicit response_message_base(response_result&& res)
            : res_(std::move(res))
        {
        }

        const response_result& result() const EWS_NOEXCEPT { return res_; }

        bool success() const EWS_NOEXCEPT
        {
            return res_.cls == response_class::success;
        }

    private:
        response_result res_;
    };

    // Base-class for response messages that contain an <Items> array.
    //
    // The ItemType template parameter denotes the type of all items in the
    // returned array. The choice for a compile-time parameter has following
    // implications and restrictions:
    //
    // - Microsoft EWS allows for different types of items in the returned
    //   array. However, this implementation forces you to only issue
    //   requests that return only one type of item in a single response at
    //   a time.
    //
    // - You need to know the type of the item returned by a request
    //   up-front at compile time. Microsoft EWS would allow to deal with
    //   different types of items in a single response dynamically.
    template <typename ItemType>
    class response_message_with_items : public response_message_base
    {
    public:
        typedef ItemType item_type;

        response_message_with_items(response_result&& res,
                                    std::vector<item_type>&& items)
            : response_message_base(std::move(res)), items_(std::move(items))
        {
        }

        const std::vector<item_type>& items() const EWS_NOEXCEPT
        {
            return items_;
        }

    private:
        std::vector<item_type> items_;
    };

    class create_folder_response_message final
        : public response_message_with_items<folder_id>
    {
    public:
        // implemented below
        static create_folder_response_message parse(http_response&&);

    private:
        create_folder_response_message(response_result&& res,
                                       std::vector<folder_id>&& items)
            : response_message_with_items<folder_id>(std::move(res),
                                                     std::move(items))
        {
        }
    };

    class create_item_response_message final
        : public response_message_with_items<item_id>
    {
    public:
        // implemented below
        static create_item_response_message parse(http_response&&);

    private:
        create_item_response_message(response_result&& res,
                                     std::vector<item_id>&& items)
            : response_message_with_items<item_id>(std::move(res),
                                                   std::move(items))
        {
        }
    };

    class find_folder_response_message final
        : public response_message_with_items<folder_id>
    {
    public:
        // implemented below
        static find_folder_response_message parse(http_response&&);

    private:
        find_folder_response_message(response_result&& res,
                                     std::vector<folder_id>&& items)
            : response_message_with_items<folder_id>(std::move(res),
                                                     std::move(items))
        {
        }
    };

    class find_item_response_message final
        : public response_message_with_items<item_id>
    {
    public:
        // implemented below
        static find_item_response_message parse(http_response&&);

    private:
        find_item_response_message(response_result&& res,
                                   std::vector<item_id>&& items)
            : response_message_with_items<item_id>(std::move(res),
                                                   std::move(items))
        {
        }
    };

    class find_calendar_item_response_message final
        : public response_message_with_items<calendar_item>
    {
    public:
        // implemented below
        static find_calendar_item_response_message parse(http_response&&);

    private:
        find_calendar_item_response_message(response_result&& res,
                                            std::vector<calendar_item>&& items)
            : response_message_with_items<calendar_item>(std::move(res),
                                                         std::move(items))
        {
        }
    };

    class update_item_response_message final
        : public response_message_with_items<item_id>
    {
    public:
        // implemented below
        static update_item_response_message parse(http_response&&);

    private:
        update_item_response_message(response_result&& res,
                                     std::vector<item_id>&& items)
            : response_message_with_items<item_id>(std::move(res),
                                                   std::move(items))
        {
        }
    };

    class update_folder_response_message final
        : public response_message_with_items<folder_id>
    {
    public:
        // implemented below
        static update_folder_response_message parse(http_response&&);

    private:
        update_folder_response_message(response_result&& res,
                                       std::vector<folder_id>&& items)
            : response_message_with_items<folder_id>(std::move(res),
                                                     std::move(items))
        {
        }
    };

    class get_folder_response_message final
        : public response_message_with_items<folder>
    {
    public:
        // implemented below
        static get_folder_response_message parse(http_response&&);

    private:
        get_folder_response_message(response_result&& res,
                                    std::vector<folder>&& items)
            : response_message_with_items<folder>(std::move(res),
                                                  std::move(items))
        {
        }
    };

    class folder_response_message final
    {
    public:
        typedef std::tuple<response_class, response_code, std::vector<folder>>
            response_message;

        std::vector<folder> items() const
        {
            std::vector<folder> items;
            items.reserve(messages_.size()); // Seems like there is always one
                                             // item per message
            for (const auto& msg : messages_)
            {
                const auto& msg_items = std::get<2>(msg);
                std::copy(begin(msg_items), end(msg_items),
                          std::back_inserter(items));
            }
            return items;
        }

        bool success() const
        {
            // Consequently, this means we're aborting here because of a
            // warning. Is this desired? Don't think so. At least it is
            // consistent with response_message_base::success().

            return std::all_of(begin(messages_), end(messages_),
                               [](const response_message& msg) {
                                   return std::get<0>(msg) ==
                                          response_class::success;
                               });
        }

        response_code first_error_or_warning() const
        {
            auto it = std::find_if_not(begin(messages_), end(messages_),
                                       [](const response_message& msg) {
                                           return std::get<0>(msg) ==
                                                  response_class::success;
                                       });
            return it == end(messages_) ? response_code::no_error
                                        : std::get<1>(*it);
        }

        // implemented below
        static folder_response_message parse(http_response&&);

    private:
        explicit folder_response_message(
            std::vector<response_message>&& messages)
            : messages_(std::move(messages))
        {
        }

        std::vector<response_message> messages_;
    };

    class get_room_lists_response_message final
        : public response_message_with_items<mailbox>
    {
    public:
        // implemented below
        static get_room_lists_response_message parse(http_response&&);

    private:
        get_room_lists_response_message(response_result&& res,
                                        std::vector<mailbox>&& items)
            : response_message_with_items<mailbox>(std::move(res),
                                                   std::move(items))
        {
        }
    };

    class get_rooms_response_message final
        : public response_message_with_items<mailbox>
    {
    public:
        // implemented below
        static get_rooms_response_message parse(http_response&&);

    private:
        get_rooms_response_message(response_result&& res,
                                   std::vector<mailbox>&& items)
            : response_message_with_items<mailbox>(std::move(res),
                                                   std::move(items))
        {
        }
    };

    template <typename ItemType>
    class get_item_response_message final
        : public response_message_with_items<ItemType>
    {
    public:
        // implemented below
        static get_item_response_message parse(http_response&&);

    private:
        get_item_response_message(response_result&& res,
                                  std::vector<ItemType>&& items)
            : response_message_with_items<ItemType>(std::move(res),
                                                    std::move(items))
        {
        }
    };

    template <typename IdType> class response_message_with_ids
    {
    public:
        typedef IdType id_type;
        typedef std::tuple<response_class, response_code, std::vector<id_type>>
            response_message;

        explicit response_message_with_ids(
            std::vector<response_message>&& messages)
            : messages_(std::move(messages))
        {
        }

        std::vector<id_type> items() const
        {
            std::vector<id_type> items;
            items.reserve(messages_.size()); // Seems like there is always one
                                             // item per message
            for (const auto& msg : messages_)
            {
                const auto& msg_items = std::get<2>(msg);
                std::copy(begin(msg_items), end(msg_items),
                          std::back_inserter(items));
            }
            return items;
        }

        bool success() const
        {
            // Consequently, this means we're aborting here because of a
            // warning. Is this desired? Don't think so. At least it is
            // consistent with response_message_base::success().

            return std::all_of(begin(messages_), end(messages_),
                               [](const response_message& msg) {
                                   return std::get<0>(msg) ==
                                          response_class::success;
                               });
        }

        response_code first_error_or_warning() const
        {
            auto it = std::find_if_not(begin(messages_), end(messages_),
                                       [](const response_message& msg) {
                                           return std::get<0>(msg) ==
                                                  response_class::success;
                                       });
            return it == end(messages_) ? response_code::no_error
                                        : std::get<1>(*it);
        }

    private:
        std::vector<response_message> messages_;
    };

    class move_item_response_message final
        : public response_message_with_ids<item_id>
    {
    public:
        // implemented below
        static move_item_response_message parse(http_response&&);

    private:
        move_item_response_message(std::vector<response_message>&& items)
            : response_message_with_ids<item_id>(std::move(items))
        {
        }
    };

    class move_folder_response_message final
        : public response_message_with_ids<folder_id>
    {
    public:
        // implemented below
        static move_folder_response_message parse(http_response&&);

    private:
        move_folder_response_message(std::vector<response_message>&& items)
            : response_message_with_ids<folder_id>(std::move(items))
        {
        }
    };

    template <typename ItemType> class item_response_messages final
    {
    public:
        typedef ItemType item_type;
        typedef std::tuple<response_class, response_code,
                           std::vector<item_type>>
            response_message;

        std::vector<item_type> items() const
        {
            std::vector<item_type> items;
            items.reserve(messages_.size()); // Seems like there is always one
                                             // item per message
            for (const auto& msg : messages_)
            {
                const auto& msg_items = std::get<2>(msg);
                std::copy(begin(msg_items), end(msg_items),
                          std::back_inserter(items));
            }
            return items;
        }

        bool success() const
        {
            // Consequently, this means we're aborting here because of a
            // warning. Is this desired? Don't think so. At least it is
            // consistent with response_message_base::success().

            return std::all_of(begin(messages_), end(messages_),
                               [](const response_message& msg) {
                                   return std::get<0>(msg) ==
                                          response_class::success;
                               });
        }

        response_code first_error_or_warning() const
        {
            auto it = std::find_if_not(begin(messages_), end(messages_),
                                       [](const response_message& msg) {
                                           return std::get<0>(msg) ==
                                                  response_class::success;
                                       });
            return it == end(messages_) ? response_code::no_error
                                        : std::get<1>(*it);
        }

        // implemented below
        static item_response_messages parse(http_response&&);

    private:
        explicit item_response_messages(
            std::vector<response_message>&& messages)
            : messages_(std::move(messages))
        {
        }

        std::vector<response_message> messages_;
    };

    class delegate_response_message : public response_message_base
    {
    public:
        const std::vector<delegate_user>& get_delegates() const EWS_NOEXCEPT
        {
            return delegates_;
        }

    protected:
        delegate_response_message(response_result&& res,
                                  std::vector<delegate_user>&& delegates)
            : response_message_base(std::move(res)),
              delegates_(std::move(delegates))
        {
        }

        // defined below
        static std::vector<delegate_user>
        parse_users(const rapidxml::xml_node<>& response_element);

        std::vector<delegate_user> delegates_;
    };

    class add_delegate_response_message final : public delegate_response_message
    {
    public:
        // defined below
        static add_delegate_response_message parse(http_response&&);

    private:
        add_delegate_response_message(response_result&& res,
                                      std::vector<delegate_user>&& delegates)
            : delegate_response_message(std::move(res), std::move(delegates))
        {
        }
    };

    class get_delegate_response_message final : public delegate_response_message
    {
    public:
        // defined below
        static get_delegate_response_message parse(http_response&&);

    private:
        get_delegate_response_message(response_result&& res,
                                      std::vector<delegate_user>&& delegates)
            : delegate_response_message(std::move(res), std::move(delegates))
        {
        }
    };

    class remove_delegate_response_message final : public response_message_base
    {
    public:
        // defined below
        static remove_delegate_response_message parse(http_response&&);

    private:
        explicit remove_delegate_response_message(response_result&& res)
            : response_message_base(std::move(res))
        {
        }
    };

    class resolve_names_response_message final : public response_message_base
    {
    public:
        // defined below
        static resolve_names_response_message parse(http_response&& response);
        const resolution_set& resolutions() const EWS_NOEXCEPT
        {
            return resolutions_;
        }

    private:
        resolve_names_response_message(response_result&& res,
                                       resolution_set&& rset)
            : response_message_base(std::move(res)),
              resolutions_(std::move(rset))
        {
        }

        resolution_set resolutions_;
    };

#ifdef EWS_HAS_VARIANT
    class subscribe_response_message final : public response_message_base
    {
    public:
        // defined below
        static subscribe_response_message parse(http_response&& response);
        const subscription_information& information() const EWS_NOEXCEPT
        {
            return information_;
        }

    private:
        subscribe_response_message(response_result&& res,
                                   subscription_information&& info)
            : response_message_base(std::move(res)),
              information_(std::move(info))
        {
        }

        subscription_information information_;
    };

    class unsubscribe_response_message final : public response_message_base
    {
    public:
        // defined below
        static unsubscribe_response_message parse(http_response&& response);

    private:
        unsubscribe_response_message(response_result&& res)
            : response_message_base(std::move(res))
        {
        }
    };

    class get_events_response_message final : public response_message_base
    {
    public:
        // defined below
        static get_events_response_message parse(http_response&& response);
        const notification& get_notification() const EWS_NOEXCEPT
        {
            return notification_;
        }

    private:
        get_events_response_message(response_result&& res, notification&& n)
            : response_message_base(std::move(res)), notification_(std::move(n))
        {
        }

        notification notification_;
    };
#endif

    class create_attachment_response_message final
        : public response_message_base
    {
    public:
        static create_attachment_response_message
        parse(http_response&& response)
        {
            const auto doc = parse_response(std::move(response));
            auto elem =
                get_element_by_qname(*doc, "CreateAttachmentResponseMessage",
                                     uri<>::microsoft::messages());

            check(elem, "Expected <CreateAttachmentResponseMessage>");

            auto result = parse_response_class_and_code(*elem);

            auto attachments_element = elem->first_node_ns(
                uri<>::microsoft::messages(), "Attachments");
            check(attachments_element, "Expected <Attachments> element");

            auto ids = std::vector<attachment_id>();
            for (auto attachment_elem = attachments_element->first_node();
                 attachment_elem;
                 attachment_elem = attachment_elem->next_sibling())
            {
                auto attachment_id_elem = attachment_elem->first_node_ns(
                    uri<>::microsoft::types(), "AttachmentId");
                check(attachment_id_elem,
                      "Expected <AttachmentId> in response");
                ids.emplace_back(
                    attachment_id::from_xml_element(*attachment_id_elem));
            }
            return create_attachment_response_message(std::move(result),
                                                      std::move(ids));
        }

        const std::vector<attachment_id>& attachment_ids() const EWS_NOEXCEPT
        {
            return ids_;
        }

    private:
        create_attachment_response_message(
            response_result&& res, std::vector<attachment_id>&& attachment_ids)
            : response_message_base(std::move(res)),
              ids_(std::move(attachment_ids))
        {
        }

        std::vector<attachment_id> ids_;
    };

    class get_attachment_response_message final : public response_message_base
    {
    public:
        static get_attachment_response_message parse(http_response&& response)
        {
            const auto doc = parse_response(std::move(response));
            auto elem =
                get_element_by_qname(*doc, "GetAttachmentResponseMessage",
                                     uri<>::microsoft::messages());

            check(elem, "Expected <GetAttachmentResponseMessage>");

            auto result = parse_response_class_and_code(*elem);

            auto attachments_element = elem->first_node_ns(
                uri<>::microsoft::messages(), "Attachments");
            check(attachments_element, "Expected <Attachments> element");
            auto attachments = std::vector<attachment>();
            for (auto attachment_elem = attachments_element->first_node();
                 attachment_elem;
                 attachment_elem = attachment_elem->next_sibling())
            {
                attachments.emplace_back(
                    attachment::from_xml_element(*attachment_elem));
            }
            return get_attachment_response_message(std::move(result),
                                                   std::move(attachments));
        }

        const std::vector<attachment>& attachments() const EWS_NOEXCEPT
        {
            return attachments_;
        }

    private:
        get_attachment_response_message(response_result&& res,
                                        std::vector<attachment>&& attachments)
            : response_message_base(std::move(res)),
              attachments_(std::move(attachments))
        {
        }

        std::vector<attachment> attachments_;
    };

    class send_item_response_message final : public response_message_base
    {
    public:
        static send_item_response_message parse(http_response&& response)
        {
            const auto doc = parse_response(std::move(response));
            auto elem = get_element_by_qname(*doc, "SendItemResponseMessage",
                                             uri<>::microsoft::messages());

            check(elem, "Expected <SendItemResponseMessage>");
            auto result = parse_response_class_and_code(*elem);
            return send_item_response_message(std::move(result));
        }

    private:
        explicit send_item_response_message(response_result&& res)
            : response_message_base(std::move(res))
        {
        }
    };

    class delete_folder_response_message final : public response_message_base
    {
    public:
        static delete_folder_response_message parse(http_response&& response)
        {
            const auto doc = parse_response(std::move(response));
            auto elem =
                get_element_by_qname(*doc, "DeleteFolderResponseMessage",
                                     uri<>::microsoft::messages());
            auto result = parse_response_class_and_code(*elem);
            check(elem, "Expected <DeleteFolderResponseMessage>");
            return delete_folder_response_message(std::move(result));
        }

    private:
        explicit delete_folder_response_message(response_result&& res)
            : response_message_base(std::move(res))
        {
        }
    };

    class delete_item_response_message final : public response_message_base
    {
    public:
        static delete_item_response_message parse(http_response&& response)
        {
            const auto doc = parse_response(std::move(response));
            auto elem = get_element_by_qname(*doc, "DeleteItemResponseMessage",
                                             uri<>::microsoft::messages());
            check(elem, "Expected <DeleteItemResponseMessage>");
            auto result = parse_response_class_and_code(*elem);
            return delete_item_response_message(std::move(result));
        }

    private:
        explicit delete_item_response_message(response_result&& res)
            : response_message_base(std::move(res))
        {
        }
    };

    class delete_attachment_response_message final
        : public response_message_base
    {
    public:
        static delete_attachment_response_message
        parse(http_response&& response)
        {
            const auto doc = parse_response(std::move(response));
            auto elem =
                get_element_by_qname(*doc, "DeleteAttachmentResponseMessage",
                                     uri<>::microsoft::messages());

            check(elem, "Expected <DeleteAttachmentResponseMessage>");
            auto result = parse_response_class_and_code(*elem);

            auto root_item_id = item_id();
            auto root_item_id_elem =
                elem->first_node_ns(uri<>::microsoft::messages(), "RootItemId");
            if (root_item_id_elem)
            {
                auto id_attr = root_item_id_elem->first_attribute("RootItemId");
                check(id_attr, "Expected RootItemId attribute");
                auto id = std::string(id_attr->value(), id_attr->value_size());

                auto change_key_attr =
                    root_item_id_elem->first_attribute("RootItemChangeKey");
                check(change_key_attr, "Expected RootItemChangeKey attribute");
                auto change_key = std::string(change_key_attr->value(),
                                              change_key_attr->value_size());

                root_item_id = item_id(std::move(id), std::move(change_key));
            }

            return delete_attachment_response_message(std::move(result),
                                                      std::move(root_item_id));
        }

        item_id get_root_item_id() const { return root_item_id_; }

    private:
        delete_attachment_response_message(response_result&& res,
                                           item_id&& root_item_id)
            : response_message_base(std::move(res)),
              root_item_id_(std::move(root_item_id))
        {
        }

        item_id root_item_id_;
    };
} // namespace internal

class sync_folder_hierarchy_result final
    : public internal::response_message_base
{
public:
#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
    // implemented below
    static sync_folder_hierarchy_result parse(internal::http_response&&);
#endif

    const std::string& get_sync_state() const EWS_NOEXCEPT
    {
        return sync_state_;
    }

    const std::vector<folder>& get_created_folders() const EWS_NOEXCEPT
    {
        return created_folders_;
    }

    const std::vector<folder>& get_updated_folders() const EWS_NOEXCEPT
    {
        return updated_folders_;
    }

    const std::vector<folder_id>& get_deleted_folder_ids() const EWS_NOEXCEPT
    {
        return deleted_folder_ids_;
    }

    bool get_includes_last_folder_in_range() const EWS_NOEXCEPT
    {
        return includes_last_folder_in_range_;
    }

private:
    explicit sync_folder_hierarchy_result(internal::response_result&& res)
        : response_message_base(std::move(res)), sync_state_(),
          created_folders_(), updated_folders_(), deleted_folder_ids_(),
          includes_last_folder_in_range_(false)
    {
    }

    std::string sync_state_;
    std::vector<folder> created_folders_;
    std::vector<folder> updated_folders_;
    std::vector<folder_id> deleted_folder_ids_;
    bool includes_last_folder_in_range_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(
    !std::is_default_constructible<sync_folder_hierarchy_result>::value, "");
static_assert(std::is_copy_constructible<sync_folder_hierarchy_result>::value);
static_assert(std::is_copy_assignable<sync_folder_hierarchy_result>::value);
static_assert(std::is_move_constructible<sync_folder_hierarchy_result>::value);
static_assert(std::is_move_assignable<sync_folder_hierarchy_result>::value);
#endif

class sync_folder_items_result final : public internal::response_message_base
{
public:
#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
    // implemented below
    static sync_folder_items_result parse(internal::http_response&&);
#endif

    const std::string& get_sync_state() const EWS_NOEXCEPT
    {
        return sync_state_;
    }

    const std::vector<item_id>& get_created_items() const EWS_NOEXCEPT
    {
        return created_items_;
    }

    const std::vector<item_id>& get_updated_items() const EWS_NOEXCEPT
    {
        return updated_items_;
    }

    const std::vector<item_id>& get_deleted_items() const EWS_NOEXCEPT
    {
        return deleted_items_;
    }

    const std::vector<std::pair<item_id, bool>>&
    get_read_flag_changed() const EWS_NOEXCEPT
    {
        return read_flag_changed_;
    }

    bool get_includes_last_item_in_range() const EWS_NOEXCEPT
    {
        return includes_last_item_in_range_;
    }

private:
    explicit sync_folder_items_result(internal::response_result&& res)
        : response_message_base(std::move(res)), sync_state_(),
          created_items_(), updated_items_(), deleted_items_(),
          read_flag_changed_(), includes_last_item_in_range_(false)
    {
    }

    std::string sync_state_;
    std::vector<item_id> created_items_;
    std::vector<item_id> updated_items_;
    std::vector<item_id> deleted_items_;
    std::vector<std::pair<item_id, bool>> read_flag_changed_;
    bool includes_last_item_in_range_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<sync_folder_items_result>::value,
              "");
static_assert(std::is_copy_constructible<sync_folder_items_result>::value);
static_assert(std::is_copy_assignable<sync_folder_items_result>::value);
static_assert(std::is_move_constructible<sync_folder_items_result>::value);
static_assert(std::is_move_assignable<sync_folder_items_result>::value);
#endif

//! \brief A thin wrapper around xs:dateTime formatted strings.
//!
//! Microsoft EWS uses date and date/time string representations as described
//! in https://www.w3.org/TR/xmlschema-2/, notably xs:dateTime and xs:date. Both
//! seem to be a subset of ISO 8601.
//!
//! For example, the lexical representation of xs:dateTime is
//!
//!     [-]CCYY-MM-DDThh:mm:ss[Z|(+|-)hh:mm]
//!
//! whereas the last part represents the time zone (as offset to UTC). The Z
//! means Zulu time which is a fancy way of meaning UTC. Two examples of date
//! strings are:
//!
//! 2000-01-16Z and 1981-07-02.
//!
//! xs:dateTime is formatted accordingly, just with a time component:
//!
//! 2001-10-26T21:32:52+02:00 and 2001-10-26T19:32:52Z.
//!
//! You get the idea.
//!
//! \note Always specify an UTC offset (and thus a time zone) when working with
//! date and time values or convert the value to UTC (passing the 'Z' flag)
//! before handing them over to EWS. Microsoft Exchange Server internally stores
//! all date and time values in UTC and will use subtle rules to convert them
//! to UTC when no UTC offset is given.
//!
//! This library does not interpret, parse, or in any way touch date nor
//! date/time strings in any circumstance. The date_time class acts solely as a
//! thin wrapper to make the signatures of public API functions more type-rich
//! and easier to understand. date_time is implicitly convertible from
//! std::string.
//!
//! If your date or date/time strings are not formatted properly, the Exchange
//! Server will likely give you a SOAP fault which this library transports to
//! you as an exception of type ews::soap_fault.
class date_time final
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    date_time() = default;
#else
    date_time() {}
#endif

    date_time(std::string str) // intentionally not explicit
        : val_(std::move(str))
    {
    }

    const std::string& to_string() const EWS_NOEXCEPT { return val_; }

    inline bool is_set() const EWS_NOEXCEPT { return !val_.empty(); }

    //! Converts this xs:dateTime to Seconds since the Epoch.
    //!
    //! Returns this date-time string's corresponding seconds since the Epoch
    //! (this value is always in UTC) expressed as a value of type time_t or
    //! throws an exception of type ews::exception if this fails for some
    //! reason.
    time_t to_epoch() const
    {
        if (!is_set())
        {
            throw exception("to_epoch called on empty date_time");
        }

        bool local_time = false;
        time_t offset = 0U; // Seconds east of UTC

        int y, M, d, h, m, tzh, tzm;
        tzh = tzm = 0;
        float s;
        char tzo;
#if _MSC_VER == 1700 || _MSC_VER == 1800
        auto res = sscanf_s(val_.c_str(), "%d-%d-%dT%d:%d:%f%c%d:%d", &y, &M,
                            &d, &h, &m, &s, &tzo, 1, &tzh, &tzm);
#else
        auto res = sscanf(val_.c_str(), "%d-%d-%dT%d:%d:%f%c%d:%d", &y, &M, &d,
                          &h, &m, &s, &tzo, &tzh, &tzm);
#endif
        if (res == EOF)
        {
            throw exception("sscanf failed");
        }
        else if (res == 6)
        {
            // without 'Z', interpreted as local time
            local_time = true;
        }
        else if (res == 7)
        {
            if (tzo == 'Z')
            {
                // UTC offset: 0
            }
            else
            {
                throw exception("to_epoch: unexpected character at match 7");
            }
        }
        else if (res == 9)
        {
            if (tzo == '-')
            {
                offset = (tzh * 60 * 60) + (tzm * 60);
            }
            else if (tzo == '+')
            {
                offset = ((tzh * 60 * 60) + (tzm * 60)) * (-1);
            }
            else
            {
                throw exception("to_epoch: unexpected character at match 7");
            }
        }
        else if (res < 6) // Whats that? Error case?
        {
            throw exception("to_epoch: could not parse string");
        }

        // The following broken-down time struct is always in local time.
        tm t;
        memset(&t, 0, sizeof(struct tm));
        t.tm_year = y - 1900;           // Year since 1900
        t.tm_mon = M - 1;               // 0-11
        t.tm_mday = d;                  // 1-31
        t.tm_hour = h;                  // 0-23
        t.tm_min = m;                   // 0-59
        t.tm_sec = static_cast<int>(s); // 0 - 61
        t.tm_isdst = -1; // Tells mktime() to determine if DST is in effect
                         // using system's TZ infos

        time_t epoch = 0L;
        if (offset == 0)
        {
            epoch = mktime(&t);
            if (epoch == -1L)
            {
                throw exception(
                    "mktime: time cannot be represented as calendar time");
            }
        }
        else
        {
#ifdef _WIN32
            // timegm is a nonstandard GNU extension that is also present on
            // some BSDs including macOS
            epoch = _mkgmtime(&t);
#else
            epoch = timegm(&t);
#endif
            if (epoch == -1L)
            {
                throw exception(
                    "timegm: time cannot be represented as calendar time");
            }
        }
        const auto bias =
            local_time ? 0L : (offset == 0L ? utc_offset(&epoch) : offset);
        return epoch + bias;
    }

    //! Constructs a xs:dateTime formatted string from given time value.
    //!
    //! The resulting string is always formatted as:
    //!
    //!    yyyy-MM-ddThh:mm:ssZ
    //!
    //! \param epoch Seconds since the Epoch (this value is always in UTC)
    //!
    //! This function throws an exception of type ews::exception if converting
    //! to a string fails.
    static date_time from_epoch(time_t epoch)
    {
#ifdef _WIN32
        tm result;
        gmtime_s(&result, &epoch);
        tm* t = &result;
#else
        tm result;
        auto t = gmtime_r(&epoch, &result);
#endif
        char buf[21];
        auto len = strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", t);
        if (len == 0)
        {
            throw exception("strftime failed");
        }
        return date_time(buf);
    }

private:
    // Compute the offset between local time and UTC in seconds for a given
    // time point. If time point is the null pointer, the current offset is
    // returned. Note: this implementation might not work when DST changes
    // during the call.
    static time_t utc_offset(const time_t* timepoint)
    {
        time_t now;
        if (timepoint == nullptr)
        {
            now = time(nullptr);
        }
        else
        {
            now = *timepoint;
        }

#ifdef _WIN32
        tm gmtime_result;
        auto gmtime_error = gmtime_s(&gmtime_result, &now);
        if (gmtime_error)
        {
            throw std::runtime_error("Invalid argument to gmtime_s");
        }
        tm* utc_time = &gmtime_result;
#else
        tm result;
        auto utc_time = gmtime_r(&now, &result);
#endif
        utc_time->tm_isdst = -1;
        const auto utc_epoch = mktime(utc_time);

#ifdef _WIN32
        tm localtime_result;
        auto localtime_error = localtime_s(&localtime_result, &now);
        if (localtime_error)
        {
            throw std::runtime_error("Invalid argument to localtime_s");
        }
        tm* local_time = &localtime_result;
#else
        auto local_time = localtime_r(&now, &result);
#endif
        local_time->tm_isdst = -1;
        const auto local_epoch = mktime(local_time);

        return local_epoch - utc_epoch;
    }

    friend bool operator==(const date_time&, const date_time&);
    std::string val_;
};

inline bool operator==(const date_time& lhs, const date_time& rhs)
{
    return lhs.val_ == rhs.val_;
}

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<date_time>::value);
static_assert(std::is_copy_constructible<date_time>::value);
static_assert(std::is_copy_assignable<date_time>::value);
static_assert(std::is_move_constructible<date_time>::value);
static_assert(std::is_move_assignable<date_time>::value);
#endif

//! \brief A xs:date formatted string
//!
//! Exactly the same type as date_time. Used to indicate that an API
//! expects a date without a time value
typedef date_time date;

//! \brief Specifies a time interval
//!
//! A thin wrapper around xs:duration formatted strings.
//!
//! The time interval is specified in the following form
//! <tt>PnYnMnDTnHnMnS</tt> where:
//!
//! \li P indicates the period(required)
//! \li nY indicates the number of years
//! \li nM indicates the number of months
//! \li nD indicates the number of days
//! \li T indicates the start of a time section (required if you are going
//!     to specify hours, minutes, or seconds)
//! \li nH indicates the number of hours
//! \li nM indicates the number of minutes
//! \li nS indicates the number of seconds
class duration final
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    duration() = default;
#else
    duration() {}
#endif

    duration(std::string str) // intentionally not explicit
        : val_(std::move(str))
    {
    }

    const std::string& to_string() const EWS_NOEXCEPT { return val_; }

    inline bool is_set() const EWS_NOEXCEPT { return !val_.empty(); }

private:
    friend bool operator==(const duration&, const duration&);
    std::string val_;
};

inline bool operator==(const duration& lhs, const duration& rhs)
{
    return lhs.val_ == rhs.val_;
}

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<duration>::value);
static_assert(std::is_copy_constructible<duration>::value);
static_assert(std::is_copy_assignable<duration>::value);
static_assert(std::is_move_constructible<duration>::value);
static_assert(std::is_move_assignable<duration>::value);
#endif

//! Specifies the type of a <tt>\<Body></tt> element
enum class body_type
{
    //! \brief The response will return the richest available content.
    //!
    //! This is useful if it is unknown whether the content is text or HTML.
    //! The returned body will be text if the stored body is plain-text.
    //! Otherwise, the response will return HTML if the stored body is in
    //! either HTML or RTF format. This is usually the default value.
    best,

    //! The response will return an item body as plain-text
    plain_text,

    //! The response will return an item body as HTML
    html
};

namespace internal
{
    inline std::string body_type_str(body_type type)
    {
        switch (type)
        {
        case body_type::best:
            return "Best";
        case body_type::plain_text:
            return "Text";
        case body_type::html:
            return "HTML";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! \brief Represents the actual content of a message.
//!
//! A \<Body/> element can be of type Best, HTML, or plain-text.
class body final
{
public:
    //! Creates an empty body element; body_type is plain-text
    body() : content_(), type_(body_type::plain_text), is_truncated_(false) {}

    //! Creates a new body element with given content and type
    explicit body(std::string content, body_type type = body_type::plain_text)
        : content_(std::move(content)), type_(type), is_truncated_(false)
    {
    }

    body_type type() const EWS_NOEXCEPT { return type_; }

    void set_type(body_type type) { type_ = type; }

    bool is_truncated() const EWS_NOEXCEPT { return is_truncated_; }

    void set_truncated(bool truncated) { is_truncated_ = truncated; }

    const std::string& content() const EWS_NOEXCEPT { return content_; }

    void set_content(std::string content) { content_ = std::move(content); }

    std::string to_xml() const
    {
        // FIXME: what about IsTruncated attribute?
        static const auto cdata_beg = std::string("<![CDATA[");
        static const auto cdata_end = std::string("]]>");

        std::stringstream sstr;
        sstr << "<t:Body BodyType=\"" << internal::body_type_str(type());
        sstr << "\">";
        if (type() == body_type::html &&
            !(content_.compare(0, cdata_beg.length(), cdata_beg) == 0))
        {
            sstr << cdata_beg << content_ << cdata_end;
        }
        else
        {
            sstr << internal::escape(content_);
        }
        sstr << "</t:Body>";
        return sstr.str();
    }

private:
    std::string content_;
    body_type type_;
    bool is_truncated_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<body>::value);
static_assert(std::is_copy_constructible<body>::value);
static_assert(std::is_copy_assignable<body>::value);
static_assert(std::is_move_constructible<body>::value);
static_assert(std::is_move_assignable<body>::value);
#endif

//! \brief Represents an item's <tt>\<MimeContent CharacterSet="" /></tt>
//! element.
//!
//! Contains the ASCII MIME stream of an object that is represented in
//! base64Binary format (as in RFC 2045).
class mime_content final
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    mime_content() = default;
#else
    mime_content() {}
#endif

    //! Copies \p len bytes from \p ptr into an internal buffer.
    mime_content(std::string charset, const char* const ptr, size_t len)
        : charset_(std::move(charset)), bytearray_(ptr, ptr + len)
    {
    }

    //! Returns how the string is encoded, e.g., "UTF-8"
    const std::string& character_set() const EWS_NOEXCEPT { return charset_; }

    //! Note: the pointer to the data is not 0-terminated
    const char* bytes() const EWS_NOEXCEPT { return bytearray_.data(); }

    size_t len_bytes() const EWS_NOEXCEPT { return bytearray_.size(); }

    //! Returns true if no MIME content is available.
    //!
    //! Note that a \<MimeContent> property is only included in a GetItem
    //! response when explicitly requested using additional properties. This
    //! function lets you test whether MIME content is available.
    bool none() const EWS_NOEXCEPT { return len_bytes() == 0U; }

private:
    std::string charset_;
    std::vector<char> bytearray_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<mime_content>::value);
static_assert(std::is_copy_constructible<mime_content>::value);
static_assert(std::is_copy_assignable<mime_content>::value);
static_assert(std::is_move_constructible<mime_content>::value);
static_assert(std::is_move_assignable<mime_content>::value);
#endif

//! \brief An attendee of a meeting or a meeting room.
//!
//! An attendee is just a mailbox for the most part. The other two
//! properties, ResponseType and LastResponseTime, are read-only properties
//! that usually get populated by the Exchange server and can be used to
//! track attendee responses.
class attendee final
{
public:
    attendee(mailbox address, response_type type, date_time last_response_time)
        : mailbox_(std::move(address)), response_type_(std::move(type)),
          last_response_time_(std::move(last_response_time))
    {
    }

    explicit attendee(mailbox address)
        : mailbox_(std::move(address)), response_type_(response_type::unknown),
          last_response_time_()
    {
    }

    //! Returns this attendee's email address
    const mailbox& get_mailbox() const EWS_NOEXCEPT { return mailbox_; }

    //! \brief Returns this attendee's response
    //!
    //! This property is only relevant to a meeting organizer's calendar
    //! item.
    response_type get_response_type() const EWS_NOEXCEPT
    {
        return response_type_;
    }

    //! Sets the atteendee's response
    void set_response_type(response_type type) { response_type_ = type; }

    //! Returns the date and time of the latest response that was received
    const date_time& get_last_response_time() const EWS_NOEXCEPT
    {
        return last_response_time_;
    }

    //! Sets the date and time fo the latest response that was received
    void set_last_response_time(const date_time& time)
    {
        last_response_time_ = time;
    }

    //! Returns the date and time that was proposed as the start by the attendee
    const date_time& get_proposed_start() const EWS_NOEXCEPT
    {
        return proposed_start_;
    }

    //! Sets the start date and time that was proposed by the attendee
    void set_proposed_start(const date_time& start) { proposed_start_ = start; }

    //! Returns the date and time that was proposed as the end by the attendee
    const date_time& get_proposed_end() const EWS_NOEXCEPT
    {
        return proposed_end_;
    }

    //! Sets the end date and time that was proposed by the attendee
    void set_proposed_end(const date_time& end) { proposed_end_ = end; }

    //! Returns the XML serialized version of this attendee instance
    std::string to_xml() const
    {
        std::stringstream sstr;
        sstr << "<t:Attendee>";
        sstr << mailbox_.to_xml();
        sstr << "<t:ResponseType>" << internal::enum_to_str(response_type_)
             << "</t:ResponseType>";
        sstr << "<t:LastResponseTime>" << last_response_time_.to_string()
             << "</t:LastResponseTime>";
        if (!proposed_start_.to_string().empty())
        {
            sstr << "<t:ProposedStart>" << proposed_start_.to_string()
                 << "</t:ProposedStart>";
        }
        if (!proposed_end_.to_string().empty())
        {
            sstr << "<t:ProposedEnd>" << proposed_end_.to_string()
                 << "</t:ProposedEnd>";
        }
        sstr << "</t:Attendee>";
        return sstr.str();
    }

    //! \brief Creates a new \<Attendee> XML element and appends it to
    //! given parent node.
    //!
    //! Returns a reference to the newly created element.
    rapidxml::xml_node<>& to_xml_element(rapidxml::xml_node<>& parent) const
    {
        check(parent.document(),
              "Parent node needs to be somewhere in a document");

        //  <Attendee>
        //    <Mailbox/>
        //    <ResponseType/>
        //    <LastResponseTime/>
        //    <ProposedStart/>
        //    <ProposedEnd/>
        //  </Attendee>

        using namespace internal;

        auto& attendee_node = create_node(parent, "t:Attendee");
        mailbox_.to_xml_element(attendee_node);

        create_node(attendee_node, "t:ResponseType",
                    enum_to_str(response_type_));
        create_node(attendee_node, "t:LastResponseTime",
                    last_response_time_.to_string());

        if (!proposed_start_.to_string().empty())
        {
            create_node(attendee_node, "t:ProposedStart",
                        proposed_start_.to_string());
        }
        if (!proposed_end_.to_string().empty())
        {
            create_node(attendee_node, "t:ProposedEnd",
                        proposed_end_.to_string());
        }

        return attendee_node;
    }

    //! Makes a attendee instance from an \<Attendee> XML element
    static attendee from_xml_element(const rapidxml::xml_node<>& elem)
    {
        using rapidxml::internal::compare;

        //  <Attendee>
        //    <Mailbox/>
        //    <ResponseType/>
        //    <LastResponseTime/>
        //    <ProposedStart/>
        //    <ProposedEnd/>
        //  </Attendee>

        auto mailbox_node =
            elem.first_node_ns(internal::uri<>::microsoft::types(), "Mailbox");
        check(mailbox_node, "Expected <Mailbox>");

        attendee attendee(mailbox::from_xml_element(*mailbox_node));

        for (auto node = elem.first_node(); node; node = node->next_sibling())
        {
            if (compare(node->local_name(), node->local_name_size(),
                        "ResponseType", strlen("ResponseType")))
            {
                attendee.set_response_type(internal::str_to_response_type(
                    std::string(node->value(), node->value_size())));
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "LastResponseTime", strlen("LastResponseTime")))
            {
                attendee.set_last_response_time(
                    date_time(std::string(node->value(), node->value_size())));
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "ProposedStart", strlen("ProposedStart")))
            {
                attendee.set_proposed_start(
                    date_time(std::string(node->value(), node->value_size())));
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "ProposedEnd", strlen("ProposedEnd")))
            {
                attendee.set_proposed_end(
                    date_time(std::string(node->value(), node->value_size())));
            }
        }

        return attendee;
    }

private:
    mailbox mailbox_;
    response_type response_type_;
    date_time last_response_time_;
    date_time proposed_start_;
    date_time proposed_end_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<attendee>::value);
static_assert(std::is_copy_constructible<attendee>::value);
static_assert(std::is_copy_assignable<attendee>::value);
static_assert(std::is_move_constructible<attendee>::value);
static_assert(std::is_move_assignable<attendee>::value);
#endif

//! \brief Represents an <tt>\<InternetMessageHeader\></tt> property.
//!
//! An instance of this class describes a single name-value pair as it is
//! found in a message's header, essentially as defined in RFC 5322 and it's
//! former revisions.
//!
//! Most standard fields are already covered by EWS properties (e.g., the
//! destination address fields "To:", "Cc:", and "Bcc:"), however, because
//! users are allowed to define custom header fields as the seem fit, you
//! can directly access message headers.
//!
//! \sa message::get_internet_message_headers
class internet_message_header final
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    internet_message_header() = delete;
#else
private:
    internet_message_header();

public:
#endif
    //! Constructs a header filed with given values
    internet_message_header(std::string name, std::string value)
        : header_name_(std::move(name)), header_value_(std::move(value))
    {
    }

    //! Returns the name of the header field
    const std::string& get_name() const EWS_NOEXCEPT { return header_name_; }

    //! Returns the value of the header field
    const std::string& get_value() const EWS_NOEXCEPT { return header_value_; }

private:
    std::string header_name_;
    std::string header_value_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<internet_message_header>::value);
static_assert(std::is_copy_constructible<internet_message_header>::value);
static_assert(std::is_copy_assignable<internet_message_header>::value);
static_assert(std::is_move_constructible<internet_message_header>::value);
static_assert(std::is_move_assignable<internet_message_header>::value);
#endif

namespace internal
{
    // Wraps a string so that it becomes its own type
    template <int tag> class str_wrapper final
    {
    public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
        str_wrapper() = default;
#else
        str_wrapper() : value_() {}
#endif

        explicit str_wrapper(std::string str) : value_(std::move(str)) {}

        const std::string& str() const EWS_NOEXCEPT { return value_; }

    private:
        std::string value_;
    };

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
    static_assert(std::is_default_constructible<str_wrapper<0>>::value);
    static_assert(std::is_copy_constructible<str_wrapper<0>>::value);
    static_assert(std::is_copy_assignable<str_wrapper<0>>::value);
    static_assert(std::is_move_constructible<str_wrapper<0>>::value);
    static_assert(std::is_move_assignable<str_wrapper<0>>::value);
#endif

    enum type_tags
    {
        distinguished_property_set_id,
        property_set_id,
        property_tag,
        property_name,
        property_id,
        property_type
    };
} // namespace internal

//! The ExtendedFieldURI element identifies an extended MAPI property
class extended_field_uri final
{
public:
#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
#    ifdef EWS_HAS_TYPE_ALIAS
    using distinguished_property_set_id =
        internal::str_wrapper<internal::distinguished_property_set_id>;
    using property_set_id = internal::str_wrapper<internal::property_set_id>;
    using property_tag = internal::str_wrapper<internal::property_tag>;
    using property_name = internal::str_wrapper<internal::property_name>;
    using property_id = internal::str_wrapper<internal::property_id>;
    using property_type = internal::str_wrapper<internal::property_type>;
#    else
    typedef internal::str_wrapper<internal::distinguished_property_set_id>
        distinguished_property_set_id;
    typedef internal::str_wrapper<internal::property_set_id> property_set_id;
    typedef internal::str_wrapper<internal::property_tag> property_tag;
    typedef internal::str_wrapper<internal::property_name> property_name;
    typedef internal::str_wrapper<internal::property_id> property_id;
    typedef internal::str_wrapper<internal::property_type> property_type;
#    endif
#endif

#ifdef EWS_HAS_DEFAULT_AND_DELETE
    extended_field_uri() = delete;
#else
private:
    extended_field_uri();

public:
#endif

    extended_field_uri(distinguished_property_set_id set_id, property_id id,
                       property_type type)
        : distinguished_set_id_(std::move(set_id)), id_(std::move(id)),
          type_(std::move(type))
    {
    }

    extended_field_uri(distinguished_property_set_id set_id, property_name name,
                       property_type type)
        : distinguished_set_id_(std::move(set_id)), name_(std::move(name)),
          type_(std::move(type))
    {
    }

    extended_field_uri(property_set_id set_id, property_id id,
                       property_type type)
        : set_id_(std::move(set_id)), id_(std::move(id)), type_(std::move(type))
    {
    }

    extended_field_uri(property_set_id set_id, property_name name,
                       property_type type)
        : set_id_(std::move(set_id)), name_(std::move(name)),
          type_(std::move(type))
    {
    }

    extended_field_uri(property_tag tag, property_type type)
        : tag_(std::move(tag)), type_(std::move(type))
    {
    }

    const std::string& get_distinguished_property_set_id() const EWS_NOEXCEPT
    {
        return distinguished_set_id_.str();
    }

    const std::string& get_property_set_id() const EWS_NOEXCEPT
    {
        return set_id_.str();
    }

    const std::string& get_property_tag() const EWS_NOEXCEPT
    {
        return tag_.str();
    }

    const std::string& get_property_name() const EWS_NOEXCEPT
    {
        return name_.str();
    }

    const std::string& get_property_id() const EWS_NOEXCEPT
    {
        return id_.str();
    }

    const std::string& get_property_type() const EWS_NOEXCEPT
    {
        return type_.str();
    }

    //! Returns a string representation of this extended_field_uri
    std::string to_xml()
    {
        std::stringstream sstr;

        sstr << "<t:ExtendedFieldURI ";

        if (!distinguished_set_id_.str().empty())
        {
            sstr << "DistinguishedPropertySetId=\""
                 << distinguished_set_id_.str() << "\" ";
        }
        if (!id_.str().empty())
        {
            sstr << "PropertyId=\"" << id_.str() << "\" ";
        }
        if (!set_id_.str().empty())
        {
            sstr << "PropertySetId=\"" << set_id_.str() << "\" ";
        }
        if (!tag_.str().empty())
        {
            sstr << "PropertyTag=\"" << tag_.str() << "\" ";
        }
        if (!name_.str().empty())
        {
            sstr << "PropertyName=\"" << name_.str() << "\" ";
        }
        if (!type_.str().empty())
        {
            sstr << "PropertyType=\"" << type_.str() << "\"/>";
        }
        return sstr.str();
    }

    //! Converts an xml string into a extended_field_uri property.
    static extended_field_uri from_xml_element(const rapidxml::xml_node<>& elem)
    {
        using rapidxml::internal::compare;

        check(compare(elem.name(), elem.name_size(), "t:ExtendedFieldURI",
                      strlen("t:ExtendedFieldURI")),
              "Expected a <ExtendedFieldURI>, got something else");

        std::string distinguished_set_id;
        std::string set_id;
        std::string tag;
        std::string name;
        std::string id;
        std::string type;

        for (auto attr = elem.first_attribute(); attr != nullptr;
             attr = attr->next_attribute())
        {
            if (compare(attr->name(), attr->name_size(),
                        "DistinguishedPropertySetId",
                        strlen("DistinguishedPropertySetId")))
            {
                distinguished_set_id =
                    std::string(attr->value(), attr->value_size());
            }
            else if (compare(attr->name(), attr->name_size(), "PropertySetId",
                             strlen("PropertySetId")))
            {
                set_id = std::string(attr->value(), attr->value_size());
            }
            else if (compare(attr->name(), attr->name_size(), "PropertyTag",
                             strlen("PropertyTag")))
            {
                tag = std::string(attr->value(), attr->value_size());
            }
            else if (compare(attr->name(), attr->name_size(), "PropertyName",
                             strlen("PropertyName")))
            {
                name = std::string(attr->value(), attr->value_size());
            }
            else if (compare(attr->name(), attr->name_size(), "PropertyId",
                             strlen("PropertyId")))
            {
                id = std::string(attr->value(), attr->value_size());
            }
            else if (compare(attr->name(), attr->name_size(), "PropertyType",
                             strlen("PropertyType")))
            {
                type = std::string(attr->value(), attr->value_size());
            }
            else
            {
                throw exception("Unexpected attribute in <ExtendedFieldURI>");
            }
        }

        check(!type.empty(), "'PropertyType' attribute missing");

        if (!distinguished_set_id.empty())
        {
            if (!id.empty())
            {
                return extended_field_uri(
                    distinguished_property_set_id(distinguished_set_id),
                    property_id(id), property_type(type));
            }
            else if (!name.empty())
            {
                return extended_field_uri(
                    distinguished_property_set_id(distinguished_set_id),
                    property_name(name), property_type(type));
            }
        }
        else if (!set_id.empty())
        {
            if (!id.empty())
            {
                return extended_field_uri(property_set_id(set_id),
                                          property_id(id), property_type(type));
            }
            else if (!name.empty())
            {
                return extended_field_uri(property_set_id(set_id),
                                          property_name(name),
                                          property_type(type));
            }
        }
        else if (!tag.empty())
        {
            return extended_field_uri(property_tag(tag), property_type(type));
        }

        throw exception(
            "Unexpected combination of <ExtendedFieldURI/> attributes");
    }

    rapidxml::xml_node<>& to_xml_element(rapidxml::xml_node<>& parent) const
    {
        auto doc = parent.document();

        auto ptr_to_node = doc->allocate_string("t:ExtendedFieldURI");
        auto new_node = doc->allocate_node(rapidxml::node_element, ptr_to_node);
        new_node->namespace_uri(internal::uri<>::microsoft::types(),
                                internal::uri<>::microsoft::types_size);

        if (!get_distinguished_property_set_id().empty())
        {
            auto ptr_to_attr =
                doc->allocate_string("DistinguishedPropertySetId");
            auto ptr_to_property = doc->allocate_string(
                get_distinguished_property_set_id().c_str());
            auto attr_new =
                doc->allocate_attribute(ptr_to_attr, ptr_to_property);
            new_node->append_attribute(attr_new);
        }

        if (!get_property_set_id().empty())
        {
            auto ptr_to_attr = doc->allocate_string("PropertySetId");
            auto ptr_to_property =
                doc->allocate_string(get_property_set_id().c_str());
            auto attr_new =
                doc->allocate_attribute(ptr_to_attr, ptr_to_property);
            new_node->append_attribute(attr_new);
        }

        if (!get_property_tag().empty())
        {
            auto ptr_to_attr = doc->allocate_string("PropertyTag");
            auto ptr_to_property =
                doc->allocate_string(get_property_tag().c_str());
            auto attr_new =
                doc->allocate_attribute(ptr_to_attr, ptr_to_property);
            new_node->append_attribute(attr_new);
        }

        if (!get_property_name().empty())
        {
            auto ptr_to_attr = doc->allocate_string("PropertyName");
            auto ptr_to_property =
                doc->allocate_string(get_property_name().c_str());
            auto attr_new =
                doc->allocate_attribute(ptr_to_attr, ptr_to_property);
            new_node->append_attribute(attr_new);
        }

        if (!get_property_type().empty())
        {
            auto ptr_to_attr = doc->allocate_string("PropertyType");
            auto ptr_to_property =
                doc->allocate_string(get_property_type().c_str());
            auto attr_new =
                doc->allocate_attribute(ptr_to_attr, ptr_to_property);
            new_node->append_attribute(attr_new);
        }

        if (!get_property_id().empty())
        {
            auto ptr_to_attr = doc->allocate_string("PropertyId");
            auto ptr_to_property =
                doc->allocate_string(get_property_id().c_str());
            auto attr_new =
                doc->allocate_attribute(ptr_to_attr, ptr_to_property);
            new_node->append_attribute(attr_new);
        }

        parent.append_node(new_node);
        return *new_node;
    }

private:
    distinguished_property_set_id distinguished_set_id_;
    property_set_id set_id_;
    property_tag tag_;
    property_name name_;
    property_id id_;
    property_type type_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<extended_field_uri>::value);
static_assert(std::is_copy_constructible<extended_field_uri>::value);
static_assert(std::is_copy_assignable<extended_field_uri>::value);
static_assert(std::is_move_constructible<extended_field_uri>::value);
static_assert(std::is_move_assignable<extended_field_uri>::value);
#endif

//! \brief Represents an <tt>\<ExtendedProperty></tt>.
//!
//! The ExtendedProperty element identifies extended MAPI properties on
//! folders and items. Extended properties enable Microsoft Exchange Server
//! clients to add customized properties to items and folders that are
//! stored in an Exchange mailbox. Custom properties can be used to store
//! data that is relevant to an object.
class extended_property final
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    extended_property() = delete;
#else
private:
    extended_property();

public:
#endif

    //! \brief Constructor to initialize an <tt>\<ExtendedProperty\></tt>
    //! with the necessary values.
    extended_property(extended_field_uri ext_field_uri,
                      std::vector<std::string> values)
        : extended_field_uri_(std::move(ext_field_uri)),
          values_(std::move(values))
    {
    }

    static extended_property
    from_xml_element(const rapidxml::xml_node<char>& node); // defined below
    std::string to_xml() const;

    //! Returns the the extended_field_uri element of this
    //! extended_property.
    const extended_field_uri& get_extended_field_uri() const EWS_NOEXCEPT
    {
        return extended_field_uri_;
    }

    //! \brief Returns the values of the extended_property as a vector
    //! even it is just one
    const std::vector<std::string>& get_values() const EWS_NOEXCEPT
    {
        return values_;
    }

private:
    extended_field_uri extended_field_uri_;
    std::vector<std::string> values_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<extended_property>::value);
static_assert(std::is_copy_constructible<extended_property>::value);
static_assert(std::is_copy_assignable<extended_property>::value);
static_assert(std::is_move_constructible<extended_property>::value);
static_assert(std::is_move_assignable<extended_property>::value);
#endif

inline std::string extended_property::to_xml() const
{
    std::stringstream sstr;

    auto values = get_values();
    auto efu = get_extended_field_uri();
    for (auto&& v : values)
    {
        sstr << efu.to_xml();
        sstr << "<t:Message>";
        sstr << "<t:ExtendedProperty>" << efu.to_xml();
        sstr << "<t:Value>" << v << "</t:Value>";
        sstr << "</t:ExtendedProperty>";
        sstr << "</t:Message>";
    }
    return sstr.str();
}

inline extended_property
extended_property::from_xml_element(const rapidxml::xml_node<char>& top_node)
{
    using namespace internal;
    using rapidxml::internal::compare;

    check(compare(top_node.name(), top_node.name_size(), "t:ExtendedProperty",
                  strlen("t:ExtendedProperty")),
          "Expected a <ExtendedProperty>, got something else");

    auto node = top_node.first_node();
    auto ext_field_uri = extended_field_uri::from_xml_element(*node);
    std::vector<std::string> values;
    node = node->next_sibling();

    if (compare(node->name(), node->name_size(), "t:Value", strlen("t:Value")))
    {
        values.emplace_back(std::string(node->value(), node->value_size()));
    }
    else if (compare(node->name(), node->name_size(), "t:Values",
                     strlen("t:Values")))
    {
        for (auto child = node->first_node(); child != nullptr;
             child = child->next_sibling())
        {
            values.emplace_back(
                std::string(child->value(), child->value_size()));
        }
    }
    return extended_property(std::move(ext_field_uri), std::move(values));
}

//! Represents a generic <tt>\<Folder></tt> in the Exchange store
class folder final
{
public:
//! Constructs a new folder
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    folder() = default;
#else
    folder() {}
#endif
    //! Constructs a new folder with the given folder_id
    explicit folder(folder_id id) : folder_id_(std::move(id)), xml_subtree_() {}

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
    folder(folder_id&& id, internal::xml_subtree&& properties)
        : folder_id_(std::move(id)), xml_subtree_(std::move(properties))
    {
    }
#endif

    //! Returns the id of a folder
    const folder_id& get_folder_id() const EWS_NOEXCEPT { return folder_id_; }

    //! Returns this folders display name
    std::string get_display_name() const
    {
        return xml().get_value_as_string("DisplayName");
    }

    //! Sets this folders display name.
    void set_display_name(const std::string& display_name)
    {
        xml().set_or_update("DisplayName", display_name);
    }

    //! Returns the total number of items in this folder.
    int get_total_count() const
    {
        return std::stoi(xml().get_value_as_string("TotalCount"));
    }

    //! Returns the number of child folders in this folder.
    int get_child_folder_count() const
    {
        return std::stoi(xml().get_value_as_string("ChildFolderCount"));
    }

    //! Returns the id of the parent folder
    folder_id get_parent_folder_id() const
    {
        auto parent_id_node = xml().get_node("ParentFolderId");
        check(parent_id_node, "Expected <ParentFolderId>");
        return folder_id::from_xml_element(*parent_id_node);
    }

    //! Returns the number of unread items in this folder
    int get_unread_count() const
    {
        return std::stoi(xml().get_value_as_string("UnreadCount"));
    }

    //! Makes a message instance from a \<Message> XML element
    static folder from_xml_element(const rapidxml::xml_node<>& elem)
    {
        auto id_node =
            elem.first_node_ns(internal::uri<>::microsoft::types(), "FolderId");
        check(id_node, "Expected <FolderId>");
        return folder(folder_id::from_xml_element(*id_node),
                      internal::xml_subtree(elem));
    }

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
protected:
    internal::xml_subtree& xml() EWS_NOEXCEPT { return xml_subtree_; }

    const internal::xml_subtree& xml() const EWS_NOEXCEPT
    {
        return xml_subtree_;
    }
#endif

private:
    template <typename U> friend class basic_service;

    folder_id folder_id_;
    internal::xml_subtree xml_subtree_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<folder>::value);
static_assert(std::is_copy_constructible<folder>::value);
static_assert(std::is_copy_assignable<folder>::value);
static_assert(std::is_move_constructible<folder>::value);
static_assert(std::is_move_assignable<folder>::value);
#endif

//! \brief Represents a generic <tt>\<Item></tt> in the Exchange store.
//!
//! Items are, along folders, the fundamental entity that is stored in
//! an Exchange store. An item can represent a mail message, an appointment, or
//! a colleague's contact data. Most of the times, you deal with those
//! specialized item types when working with the EWS API. In some cases though,
//! it is easier to use the more general item class directly.
//!
//! The item base-class contains all properties that are common among all
//! concrete sub-classes, most notably the `<Subject>`, `<Body>`, and
//! `<ItemId>` properties.
//!
//! Like folders, each item that exists in an Exchange store has a unique
//! identifier attached to it. This is represented by the \ref item_id class and
//! you'd use and item's \ref get_item_id member-function to obtain a reference
//! to it.
class item
{
public:
//! Constructs a new item
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    item() = default;
#else
    item() {}
#endif
    //! Constructs a new item with the given id
    explicit item(item_id id) : item_id_(std::move(id)), xml_subtree_() {}

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
    item(item_id&& id, internal::xml_subtree&& properties)
        : item_id_(std::move(id)), xml_subtree_(std::move(properties))
    {
    }
#endif

    //! Returns the id of an item
    const item_id& get_item_id() const EWS_NOEXCEPT { return item_id_; }

    //! Base64-encoded contents of the MIME stream of this item
    mime_content get_mime_content() const
    {
        const auto node = xml().get_node("MimeContent");
        if (!node)
        {
            return mime_content();
        }
        auto charset = node->first_attribute("CharacterSet");
        check(charset, "Expected <MimeContent> to have CharacterSet attribute");
        return mime_content(charset->value(), node->value(),
                            node->value_size());
    }

    //! \brief Returns a unique identifier for the folder that contains
    //! this item
    //!
    //! This is a read-only property.
    folder_id get_parent_folder_id() const
    {
        const auto node = xml().get_node("ParentFolderId");
        return node ? folder_id::from_xml_element(*node) : folder_id();
    }

    //! \brief Returns the PR_MESSAGE_CLASS MAPI property (the message
    //! class) for an item
    std::string get_item_class() const
    {
        return xml().get_value_as_string("ItemClass");
    }

    //! Sets this item's subject. Limited to 255 characters.
    void set_subject(const std::string& subject)
    {
        xml().set_or_update("Subject", subject);
    }

    //! Returns this item's subject
    std::string get_subject() const
    {
        return xml().get_value_as_string("Subject");
    }

    //! Returns the sensitivity level of this item
    sensitivity get_sensitivity() const
    {
        const auto val = xml().get_value_as_string("Sensitivity");
        return !val.empty() ? internal::str_to_sensitivity(val)
                            : sensitivity::normal;
    }

    //! Sets the sensitivity level of this item
    void set_sensitivity(sensitivity s)
    {
        xml().set_or_update("Sensitivity", internal::enum_to_str(s));
    }

    //! Sets the body of this item
    void set_body(const body& b)
    {
        auto doc = xml().document();

        auto body_node = xml().get_node("Body");
        if (body_node)
        {
            doc->remove_node(body_node);
        }

        body_node = &internal::create_node(*doc, "t:Body", b.content());

        auto ptr_to_key = doc->allocate_string("BodyType");
        auto ptr_to_value =
            doc->allocate_string(internal::body_type_str(b.type()).c_str());
        body_node->append_attribute(
            doc->allocate_attribute(ptr_to_key, ptr_to_value));

        bool truncated = b.is_truncated();
        if (truncated)
        {
            ptr_to_key = doc->allocate_string("IsTruncated");
            ptr_to_value = doc->allocate_string(truncated ? "true" : "false");
            body_node->append_attribute(
                doc->allocate_attribute(ptr_to_key, ptr_to_value));
        }
    }

    //! Returns the body of this item
    body get_body() const
    {
        using rapidxml::internal::compare;

        body b;
        auto body_node = xml().get_node("Body");
        if (body_node)
        {
            for (auto attr = body_node->first_attribute(); attr;
                 attr = attr->next_attribute())
            {
                if (compare(attr->name(), attr->name_size(), "BodyType",
                            strlen("BodyType")))
                {
                    if (compare(attr->value(), attr->value_size(), "HTML",
                                strlen("HTML")))
                    {
                        b.set_type(body_type::html);
                    }
                    else if (compare(attr->value(), attr->value_size(), "Text",
                                     strlen("Text")))
                    {
                        b.set_type(body_type::plain_text);
                    }
                    else if (compare(attr->value(), attr->value_size(), "Best",
                                     strlen("Best")))
                    {
                        b.set_type(body_type::best);
                    }
                    else
                    {
                        check(false, "Unexpected attribute value for BodyType");
                    }
                }
                else if (compare(attr->name(), attr->name_size(), "IsTruncated",
                                 strlen("IsTruncated")))
                {
                    const auto val = compare(attr->value(), attr->value_size(),
                                             "true", strlen("true"));
                    b.set_truncated(val);
                }
                else
                {
                    check(false, "Unexpected attribute in <Body> element");
                }
            }

            auto content =
                std::string(body_node->value(), body_node->value_size());
            b.set_content(std::move(content));
        }
        return b;
    }

    //! Returns the items or files that are attached to this item
    std::vector<attachment> get_attachments() const
    {
        // TODO: support attachment hierarchies

        const auto attachments_node = xml().get_node("Attachments");
        if (!attachments_node)
        {
            return std::vector<attachment>();
        }

        std::vector<attachment> attachments;
        for (auto child = attachments_node->first_node(); child != nullptr;
             child = child->next_sibling())
        {
            attachments.emplace_back(attachment::from_xml_element(*child));
        }
        return attachments;
    }

    //! \brief Date/Time an item was received.
    //!
    //! This is a read-only property.
    date_time get_date_time_received() const
    {
        const auto val = xml().get_value_as_string("DateTimeReceived");
        return !val.empty() ? date_time(val) : date_time();
    }

    //! \brief Size in bytes of an item.
    //!
    //! This is a read-only property. Default: 0
    size_t get_size() const
    {
        const auto size = xml().get_value_as_string("Size");
        return size.empty() ? 0U : std::stoul(size);
    }

    //! \brief Sets this item's categories.
    //!
    //! A category is a short user-defined string that groups items with
    //! the same category together. An item can have none or multiple
    //! categories assigned. Think of tags or Google Mail labels.
    //!
    //! \sa item::get_categories
    void set_categories(const std::vector<std::string>& categories)
    {
        auto doc = xml().document();
        auto target_node = xml().get_node("Categories");
        if (!target_node)
        {
            target_node = &internal::create_node(*doc, "t:Categories");
        }

        for (const auto& category : categories)
        {
            internal::create_node(*target_node, "t:String", category);
        }
    }

    //! \brief Returns the categories associated with this item.
    //!
    //! \sa item::set_categories
    std::vector<std::string> get_categories() const
    {
        const auto categories_node = xml().get_node("Categories");
        if (!categories_node)
        {
            return std::vector<std::string>();
        }

        std::vector<std::string> categories;
        for (auto child = categories_node->first_node(); child != nullptr;
             child = child->next_sibling())
        {
            categories.emplace_back(
                std::string(child->value(), child->value_size()));
        }
        return categories;
    }

    //! Sets the importance of the item
    void set_importance(importance i)
    {
        xml().set_or_update("Importance", internal::enum_to_str(i));
    }

    //! Returns the importance of this item
    importance get_importance() const
    {
        const auto val = xml().get_value_as_string("Importance");
        return !val.empty() ? internal::str_to_importance(val)
                            : importance::normal;
    }

    //! \brief Returns the identifier of the item to which this item is a
    //! reply.
    //!
    //! This is a read-only property.
    std::string get_in_reply_to() const
    {
        return xml().get_value_as_string("InReplyTo");
    }

    //! \brief True if this item has been submitted for delivery.
    //!
    //! Default: false.
    bool is_submitted() const
    {
        return xml().get_value_as_string("IsSubmitted") == "true";
    }

    //! \brief True if this item is a draft.
    //!
    //! Default: false
    bool is_draft() const
    {
        return xml().get_value_as_string("IsDraft") == "true";
    }

    //! \brief True if this item is from you.
    //!
    //! Default: false.
    bool is_from_me() const
    {
        return xml().get_value_as_string("IsFromMe") == "true";
    }

    //! \brief True if this item a re-send.
    //!
    //! Default: false
    bool is_resend() const
    {
        return xml().get_value_as_string("IsResend") == "true";
    }

    //! \brief True if this item is unmodified.
    //!
    //! Default: false.
    bool is_unmodified() const
    {
        return xml().get_value_as_string("IsUnmodified") == "true";
    }

    //! \brief Returns a collection of Internet message headers associated
    //! with this item.
    //!
    //! This is a read-only property.
    //!
    //! \sa internet_message_header
    std::vector<internet_message_header> get_internet_message_headers() const
    {
        const auto node = xml().get_node("InternetMessageHeaders");
        if (!node)
        {
            return std::vector<internet_message_header>();
        }

        std::vector<internet_message_header> headers;
        for (auto child = node->first_node(); child != nullptr;
             child = child->next_sibling())
        {
            auto field = std::string(child->first_attribute()->value(),
                                     child->first_attribute()->value_size());
            auto value = std::string(child->value(), child->value_size());

            headers.emplace_back(
                internet_message_header(std::move(field), std::move(value)));
        }

        return headers;
    }

    //! \brief Returns the date/time this item was sent.
    //!
    //! This is a read-only property.
    date_time get_date_time_sent() const
    {
        return date_time(xml().get_value_as_string("DateTimeSent"));
    }

    //! \brief Returns the date/time this item was created.
    //!
    //! This is a read-only property.
    date_time get_date_time_created() const
    {
        return date_time(xml().get_value_as_string("DateTimeCreated"));
    }

    // Applicable response actions for this item
    // (NonEmptyArrayOfResponseObjectsType)
    // TODO: get_response_objects

    //! \brief Sets the due date of this item.
    //!
    //! Used for reminders.
    void set_reminder_due_by(const date_time& due_by)
    {
        xml().set_or_update("ReminderDueBy", due_by.to_string());
    }

    //! \brief Returns the due date of this item
    //!
    //! \sa item::set_reminder_due_by
    date_time get_reminder_due_by() const
    {
        return date_time(xml().get_value_as_string("ReminderDueBy"));
    }

    //! Set a reminder on this item
    void set_reminder_enabled(bool enabled)
    {
        xml().set_or_update("ReminderIsSet", enabled ? "true" : "false");
    }

    //! True if a reminder has been enabled on this item
    bool is_reminder_enabled() const
    {
        return xml().get_value_as_string("ReminderIsSet") == "true";
    }

    //! \brief Sets the minutes before due date that a reminder should be
    //! shown to the user.
    void set_reminder_minutes_before_start(uint32_t minutes)
    {
        xml().set_or_update("ReminderMinutesBeforeStart",
                            std::to_string(minutes));
    }

    //! \brief Returns the number of minutes before due date that a
    //! reminder should be shown to the user.
    uint32_t get_reminder_minutes_before_start() const
    {
        std::string minutes =
            xml().get_value_as_string("ReminderMinutesBeforeStart");
        return minutes.empty() ? 0U : std::stoul(minutes);
    }

    //! \brief Returns a nice string containing all Cc: recipients of this
    //! item.
    //!
    //! The \<DisplayCc/> property is a concatenated string of the display
    //! names of the Cc: recipients of an item. Each recipient is separated
    //! by a semicolon. This is a read-only property.
    std::string get_display_cc() const
    {
        return xml().get_value_as_string("DisplayCc");
    }

    //! \brief Returns a nice string containing all To: recipients of this
    //! item.
    //!
    //! The \<DisplayTo/> property is a concatenated string of the display
    //! names of all the To: recipients of an item. Each recipient is
    //! separated by a semicolon. This is a read-only property.
    std::string get_display_to() const
    {
        return xml().get_value_as_string("DisplayTo");
    }

    //! \brief True if this item has non-hidden attachments.
    //!
    //! This is a read-only property.
    bool has_attachments() const
    {
        return xml().get_value_as_string("HasAttachments") == "true";
    }

    //! List of zero or more extended properties that are requested for
    //! an item
    std::vector<extended_property> get_extended_properties() const
    {
        using rapidxml::internal::compare;

        std::vector<extended_property> properties;

        for (auto top_node = xml().get_node("ExtendedProperty");
             top_node != nullptr; top_node = top_node->next_sibling())
        {
            if (!compare(top_node->name(), top_node->name_size(),
                         "t:ExtendedProperty", strlen("t:ExtendedProperty")))
            {
                continue;
            }

            auto ext_prop = extended_property::from_xml_element(*top_node);
            properties.emplace_back(ext_prop);
        }

        return properties;
    }

    //! Sets an extended property of an item
    void set_extended_property(const extended_property& extended_prop)
    {
        auto doc = xml().document();

        auto ptr_to_qname = doc->allocate_string("t:ExtendedProperty");
        auto top_node = doc->allocate_node(rapidxml::node_element);
        top_node->qname(ptr_to_qname, strlen("t:ExtendedProperty"),
                        ptr_to_qname + 2);
        top_node->namespace_uri(internal::uri<>::microsoft::types(),
                                internal::uri<>::microsoft::types_size);
        doc->append_node(top_node);

        extended_field_uri field_uri = extended_prop.get_extended_field_uri();
        field_uri.to_xml_element(*top_node);

        rapidxml::xml_node<>* cover_node = nullptr;
        if (extended_prop.get_values().size() > 1)
        {
            auto ptr_to_values = doc->allocate_string("t:Values");
            cover_node =
                doc->allocate_node(rapidxml::node_element, ptr_to_values);
            cover_node->namespace_uri(internal::uri<>::microsoft::types(),
                                      internal::uri<>::microsoft::types_size);
            top_node->append_node(cover_node);
        }

        for (auto str : extended_prop.get_values())
        {
            auto new_str = doc->allocate_string(str.c_str());
            auto ptr_to_value = doc->allocate_string("t:Value");
            auto cur_node =
                doc->allocate_node(rapidxml::node_element, ptr_to_value);
            cur_node->namespace_uri(internal::uri<>::microsoft::types(),
                                    internal::uri<>::microsoft::types_size);
            cur_node->value(new_str);

            cover_node == nullptr ? top_node->append_node(cur_node)
                                  : cover_node->append_node(cur_node);
        }
    }

    //! Sets the culture name associated with the body of this item
    void set_culture(const std::string& culture)
    {
        xml().set_or_update("Culture", culture);
    }

    //! Returns the culture name associated with the body of this item
    std::string get_culture() const
    {
        return xml().get_value_as_string("Culture");
    }

    // Following properties are beyond 2007 scope:
    //   <EffectiveRights/>
    //   <LastModifiedName/>
    //   <LastModifiedTime/>
    //   <IsAssociated/>
    //   <WebClientReadFormQueryString/>
    //   <WebClientEditFormQueryString/>
    //   <ConversationId/>
    //   <UniqueBody/>

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
protected:
    internal::xml_subtree& xml() EWS_NOEXCEPT { return xml_subtree_; }

    const internal::xml_subtree& xml() const EWS_NOEXCEPT
    {
        return xml_subtree_;
    }
#endif

    void set_array_of_strings_helper(const std::vector<std::string>& strings,
                                     const char* name)
    {
        // TODO: this does not meet strong exception safety guarantees

        using namespace internal;

        auto outer_node = xml().get_node(name);
        if (outer_node)
        {
            auto doc = outer_node->document();
            doc->remove_node(outer_node);
        }

        if (strings.empty())
        {
            // Nothing to do
            return;
        }

        outer_node = &create_node(*xml().document(), std::string("t:") + name);
        for (const auto& element : strings)
        {
            create_node(*outer_node, "t:String", element);
        }
    }

    std::vector<std::string> get_array_of_strings_helper(const char* name) const
    {
        auto node = xml().get_node(name);
        if (!node)
        {
            return std::vector<std::string>();
        }
        auto res = std::vector<std::string>();
        for (auto entry = node->first_node(); entry;
             entry = entry->next_sibling())
        {
            res.emplace_back(std::string(entry->value(), entry->value_size()));
        }
        return res;
    }

private:
    friend class attachment;

    item_id item_id_;
    internal::xml_subtree xml_subtree_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<item>::value);
static_assert(std::is_copy_constructible<item>::value);
static_assert(std::is_copy_assignable<item>::value);
static_assert(std::is_move_constructible<item>::value);
static_assert(std::is_move_assignable<item>::value);
#endif

class user_id final
{
public:
    // Default or Anonymous
    enum class distinguished_user
    {
        default_user_account,
        anonymous
    };

#ifdef EWS_HAS_DEFAULT_AND_DELETE
    user_id() = default;
#else
    user_id() {}
#endif

    user_id(std::string sid, std::string primary_smtp_address,
            std::string display_name)
        : sid_(std::move(sid)),
          primary_smtp_address_(std::move(primary_smtp_address)),
          display_name_(std::move(display_name)), distinguished_user_(),
          external_user_identity_(false)
    {
    }

    user_id(std::string sid, std::string primary_smtp_address,
            std::string display_name, distinguished_user user_account,
            bool external_user_identity)
        : sid_(std::move(sid)),
          primary_smtp_address_(std::move(primary_smtp_address)),
          display_name_(std::move(display_name)),
          distinguished_user_(std::move(user_account)),
          external_user_identity_(std::move(external_user_identity))
    {
    }

    const std::string& get_sid() const EWS_NOEXCEPT { return sid_; }

    const std::string& get_primary_smtp_address() const EWS_NOEXCEPT
    {
        return primary_smtp_address_;
    }

    const std::string& get_display_name() const EWS_NOEXCEPT
    {
        return display_name_;
    }

#ifdef EWS_HAS_OPTIONAL
    std::optional<distinguished_user>
#else
    internal::optional<distinguished_user>
#endif
    get_distinguished_user() const EWS_NOEXCEPT
    {
        return distinguished_user_;
    }

    bool is_external_user_identity() const EWS_NOEXCEPT
    {
        return external_user_identity_;
    }

    //! Creates a user_id from a given SMTP address.
    static user_id from_primary_smtp_address(std::string primary_smtp_address)
    {
        return user_id(std::string(), std::move(primary_smtp_address),
                       std::string());
    }

    //! Creates a user_id from a given SID.
    static user_id from_sid(std::string sid)
    {
        return user_id(std::move(sid), std::string(), std::string());
    }

    static user_id from_xml_element(const rapidxml::xml_node<char>& elem)
    {
        using rapidxml::internal::compare;

        // <UserId>
        //    <SID/>
        //    <PrimarySmtpAddress/>
        //    <DisplayName/>
        //    <DistinguishedUser/>
        //    <ExternalUserIdentity/>
        // </UserId>

        std::string sid;
        std::string primary_smtp_address;
        std::string display_name;
        distinguished_user user_account =
            distinguished_user::default_user_account;
        bool external_user_identity = false;

        for (auto node = elem.first_node(); node; node = node->next_sibling())
        {
            if (compare(node->local_name(), node->local_name_size(), "SID",
                        strlen("SID")))
            {
                sid = std::string(node->value(), node->value_size());
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "PrimarySmtpAddress",
                             strlen("PrimarySmtpAddress")))
            {
                primary_smtp_address =
                    std::string(node->value(), node->value_size());
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "DisplayName", strlen("DisplayName")))
            {
                display_name = std::string(node->value(), node->value_size());
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "DistinguishedUser", strlen("DistinguishedUser")))
            {
                const auto val = std::string(node->value(), node->value_size());
                if (val != "Anonymous")
                {
                    user_account = distinguished_user::anonymous;
                }
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "ExternalUserIdentity",
                             strlen("ExternalUserIdentity")))
            {
                external_user_identity = true;
            }
            else
            {
                throw exception("Unexpected child element in <UserId>");
            }
        }

        return user_id(sid, primary_smtp_address, display_name, user_account,
                       external_user_identity);
    }

    std::string to_xml() const
    {
        std::stringstream sstr;
        sstr << "<t:UserId>";

        if (!sid_.empty())
        {
            sstr << "<t:SID>" << sid_ << "</t:SID>";
        }

        if (!primary_smtp_address_.empty())
        {
            sstr << "<t:PrimarySmtpAddress>" << primary_smtp_address_
                 << "</t:PrimarySmtpAddress>";
        }

        if (!display_name_.empty())
        {
            sstr << "<t:DisplayName>" << display_name_ << "</t:DisplayName>";
        }

        if (distinguished_user_.has_value())
        {
            sstr << "<t:DistinguishedUser>"
                 << (distinguished_user_ == distinguished_user::anonymous
                         ? "Anonymous"
                         : "Default")
                 << "</t:DistinguishedUser>";
        }

        if (external_user_identity_)
        {
            sstr << "<t:ExternalUserIdentity/>";
        }

        sstr << "</t:UserId>";
        return sstr.str();
    }

private:
    std::string sid_;
    std::string primary_smtp_address_;
    std::string display_name_;
#ifdef EWS_HAS_OPTIONAL
    std::optional<distinguished_user> distinguished_user_;
#else
    internal::optional<distinguished_user> distinguished_user_;
#endif
    bool external_user_identity_; // Renders to an element without text value,
                                  // if set
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<user_id>::value);
static_assert(std::is_copy_constructible<user_id>::value);
static_assert(std::is_copy_assignable<user_id>::value);
static_assert(std::is_move_constructible<user_id>::value);
static_assert(std::is_move_assignable<user_id>::value);
#endif

//! Represents a single delegate.
class delegate_user final
{
public:
    //! Specifies the delegate permission-level settings for a user
    enum class permission_level
    {
        //! Access to items is prohibited
        none,

        //! Can read items
        reviewer,

        //! Can read and create items
        author,

        //! Can read, create, and modify items
        editor,

        //! No idea
        custom
    };

    struct delegate_permissions final
    {
        permission_level calendar_folder;
        permission_level tasks_folder;
        permission_level inbox_folder;
        permission_level contacts_folder;
        permission_level notes_folder;
        permission_level journal_folder;

        delegate_permissions()
            : calendar_folder(permission_level::none),
              tasks_folder(permission_level::none),
              inbox_folder(permission_level::none),
              contacts_folder(permission_level::none),
              notes_folder(permission_level::none),
              journal_folder(permission_level::none)
        {
        }

        // defined below
        std::string to_xml() const;

        // defined below
        static delegate_permissions
        from_xml_element(const rapidxml::xml_node<char>& elem);
    };

#ifdef EWS_HAS_DEFAULT_AND_DELETE
    delegate_user() = default;
#else
    delegate_user() {}
#endif

    delegate_user(user_id user, delegate_permissions permissions,
                  bool receive_copies, bool view_private_items)
        : user_id_(std::move(user)), permissions_(std::move(permissions)),
          receive_copies_(std::move(receive_copies)),
          view_private_items_(std::move(view_private_items))
    {
    }

    const user_id& get_user_id() const EWS_NOEXCEPT { return user_id_; }

    const delegate_permissions& get_permissions() const EWS_NOEXCEPT
    {
        return permissions_;
    }

    //! \brief Returns whether this delegate receives copies of meeting-related
    //! messages that are addressed to the original owner of the mailbox.
    bool get_receive_copies_of_meeting_messages() const EWS_NOEXCEPT
    {
        return receive_copies_;
    }

    //! \brief Returns whether this delegate is allowed to view private items in
    //! the owner's mailbox.
    bool get_view_private_items() const EWS_NOEXCEPT
    {
        return view_private_items_;
    }

    static delegate_user from_xml_element(const rapidxml::xml_node<char>& elem)
    {
        // <DelegateUser>
        //    <UserId/>
        //    <DelegatePermissions/>
        //    <ReceiveCopiesOfMeetingMessages/>
        //    <ViewPrivateItems/>
        // </DelegateUser>

        using rapidxml::internal::compare;

        user_id id;
        delegate_permissions perms;
        bool receive_copies = false;
        bool view_private_items = false;

        for (auto node = elem.first_node(); node; node = node->next_sibling())
        {
            if (compare(node->local_name(), node->local_name_size(), "UserId",
                        strlen("UserId")))
            {
                id = user_id::from_xml_element(*node);
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "DelegatePermissions",
                             strlen("DelegatePermissions")))
            {
                perms = delegate_permissions::from_xml_element(*node);
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "ReceiveCopiesOfMeetingMessages",
                             strlen("ReceiveCopiesOfMeetingMessages")))
            {
                receive_copies = true;
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "ViewPrivateItems", strlen("ViewPrivateItems")))
            {
                view_private_items = true;
            }
            else
            {
                throw exception("Unexpected child element in <DelegateUser>");
            }
        }

        return delegate_user(id, perms, receive_copies, view_private_items);
    }

    std::string to_xml() const
    {
        std::stringstream sstr;
        sstr << "<t:DelegateUser>";
        sstr << user_id_.to_xml();
        sstr << permissions_.to_xml();
        sstr << "<t:ReceiveCopiesOfMeetingMessages>"
             << (receive_copies_ ? "true" : "false")
             << "</t:ReceiveCopiesOfMeetingMessages>";
        sstr << "<t:ViewPrivateItems>"
             << (view_private_items_ ? "true" : "false")
             << "</t:ViewPrivateItems>";
        sstr << "</t:DelegateUser>";
        return sstr.str();
    }

private:
    user_id user_id_;
    delegate_permissions permissions_;
    bool receive_copies_;
    bool view_private_items_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<delegate_user>::value);
static_assert(std::is_copy_constructible<delegate_user>::value);
static_assert(std::is_copy_assignable<delegate_user>::value);
static_assert(std::is_move_constructible<delegate_user>::value);
static_assert(std::is_move_assignable<delegate_user>::value);
#endif

namespace internal
{
    inline std::string enum_to_str(delegate_user::permission_level level)
    {
        std::string str;
        switch (level)
        {
        case delegate_user::permission_level::none:
            str = "None";
            break;

        case delegate_user::permission_level::editor:
            str = "Editor";
            break;

        case delegate_user::permission_level::author:
            str = "Author";
            break;

        case delegate_user::permission_level::reviewer:
            str = "Reviewer";
            break;

        case delegate_user::permission_level::custom:
            str = "Custom";
            break;

        default:
            throw exception("Unknown permission level");
        }
        return str;
    }

    inline delegate_user::permission_level
    str_to_permission_level(const std::string& str)
    {
        auto level = delegate_user::permission_level::none;
        if (str == "Editor")
        {
            level = delegate_user::permission_level::editor;
        }
        else if (str == "Author")
        {
            level = delegate_user::permission_level::author;
        }
        else if (str == "Reviewer")
        {
            level = delegate_user::permission_level::reviewer;
        }
        else if (str == "Custom")
        {
            level = delegate_user::permission_level::custom;
        }
        return level;
    }
} // namespace internal

//! \brief Describes the state of a delegated task.
//!
//! Values indicate whether the delegated task was accepted or not.
enum class delegation_state
{
    //! \brief The task is not a delegated task, or the task request has
    //! been created but not sent
    no_match,

    //! \brief The task is new and the request has been sent, but the
    //! delegate has no yet responded to the task
    own_new,

    //! Should not be used
    owned,

    //! The task was accepted by the delegate
    accepted,

    //! The task was declined by the delegate
    declined,

    //! Should not be used
    max
};

namespace internal
{
    inline std::string enum_to_str(delegation_state state)
    {
        switch (state)
        {
        case delegation_state::no_match:
            return "NoMatch";
        case delegation_state::own_new:
            return "OwnNew";
        case delegation_state::owned:
            return "Owned";
        case delegation_state::accepted:
            return "Accepted";
        case delegation_state::declined:
            return "Declined";
        case delegation_state::max:
            return "Max";
        default:
            throw exception("Unexpected <DelegationState>");
        }
    }
} // namespace internal

//! Specifies the status of a task item
enum class status
{
    //! The task is not started
    not_started,

    //! The task is started and in progress
    in_progress,

    //! The task is completed
    completed,

    //! The task is waiting on other
    waiting_on_others,

    //! The task is deferred
    deferred
};

namespace internal
{
    inline std::string enum_to_str(status s)
    {
        switch (s)
        {
        case status::not_started:
            return "NotStarted";
        case status::in_progress:
            return "InProgress";
        case status::completed:
            return "Completed";
        case status::waiting_on_others:
            return "WaitingOnOthers";
        case status::deferred:
            return "Deferred";
        default:
            throw exception("Unexpected <Status>");
        }
    }
} // namespace internal

//! Describes the month when a yearly recurring item occurs
enum class month
{
    //! January
    jan,

    //! February
    feb,

    //! March
    mar,

    //! April
    apr,

    //! May
    may,

    //! June
    june,

    //! July
    july,

    //! August
    aug,

    //! September
    sept,

    //! October
    oct,

    //! November
    nov,

    //! December
    dec
};

namespace internal
{
    inline std::string enum_to_str(month m)
    {
        switch (m)
        {
        case month::jan:
            return "January";
        case month::feb:
            return "February";
        case month::mar:
            return "March";
        case month::apr:
            return "April";
        case month::may:
            return "May";
        case month::june:
            return "June";
        case month::july:
            return "July";
        case month::aug:
            return "August";
        case month::sept:
            return "September";
        case month::oct:
            return "October";
        case month::nov:
            return "November";
        case month::dec:
            return "December";
        default:
            throw exception("Unexpected <Month>");
        }
    }

    inline month str_to_month(const std::string& str)
    {
        month mon;
        if (str == "January")
        {
            mon = month::jan;
        }
        else if (str == "February")
        {
            mon = month::feb;
        }
        else if (str == "March")
        {
            mon = month::mar;
        }
        else if (str == "April")
        {
            mon = month::apr;
        }
        else if (str == "May")
        {
            mon = month::may;
        }
        else if (str == "June")
        {
            mon = month::june;
        }
        else if (str == "July")
        {
            mon = month::july;
        }
        else if (str == "August")
        {
            mon = month::aug;
        }
        else if (str == "September")
        {
            mon = month::sept;
        }
        else if (str == "October")
        {
            mon = month::oct;
        }
        else if (str == "November")
        {
            mon = month::nov;
        }
        else if (str == "December")
        {
            mon = month::dec;
        }
        else
        {
            throw exception("Unexpected <Month>");
        }
        return mon;
    }
} // namespace internal

//! Describes working days
enum class day_of_week
{
    //! Sunday
    sun,

    //! Monday
    mon,

    //! Tuesday
    tue,

    //! Wednesday
    wed,

    //! Thursday
    thu,

    //! Friday
    fri,

    //! Saturday
    sat,

    //! Any day
    day,

    //! A weekday
    weekday,

    //! A day weekend
    weekend_day
};

namespace internal
{
    inline std::string enum_to_str(day_of_week d)
    {
        switch (d)
        {
        case day_of_week::sun:
            return "Sunday";
        case day_of_week::mon:
            return "Monday";
        case day_of_week::tue:
            return "Tuesday";
        case day_of_week::wed:
            return "Wednesday";
        case day_of_week::thu:
            return "Thursday";
        case day_of_week::fri:
            return "Friday";
        case day_of_week::sat:
            return "Saturday";
        case day_of_week::day:
            return "Day";
        case day_of_week::weekday:
            return "Weekday";
        case day_of_week::weekend_day:
            return "WeekendDay";
        default:
            throw exception("Unexpected <DaysOfWeek>");
        }
    }

    inline day_of_week str_to_day_of_week(const std::string& str)
    {
        if (str == "Sunday")
        {
            return day_of_week::sun;
        }
        else if (str == "Monday")
        {
            return day_of_week::mon;
        }
        else if (str == "Tuesday")
        {
            return day_of_week::tue;
        }
        else if (str == "Wednesday")
        {
            return day_of_week::wed;
        }
        else if (str == "Thursday")
        {
            return day_of_week::thu;
        }
        else if (str == "Friday")
        {
            return day_of_week::fri;
        }
        else if (str == "Saturday")
        {
            return day_of_week::sat;
        }
        else if (str == "Day")
        {
            return day_of_week::day;
        }
        else if (str == "Weekday")
        {
            return day_of_week::weekday;
        }
        else if (str == "WeekendDay")
        {
            return day_of_week::weekend_day;
        }
        else
        {
            throw exception("Unexpected <DaysOfWeek>");
        }
    }
} // namespace internal

//! \brief This element describes which week in a month is used in a
//! relative recurrence pattern.
//!
//! For example, the second Monday of a month may occur in the third week
//! of that month. If a month starts on a Friday, the first week of the
//! month only contains a few days and does not contain a Monday.
//! Therefore, the first Monday would have to occur in the second week.
enum class day_of_week_index
{
    //! The first occurrence of a day within a month
    first,

    //! The second occurrence of a day within a month
    second,

    //! The third occurrence of a day within a month
    third,

    //! The fourth occurrence of a day within a month
    fourth,

    //! The last occurrence of a day within a month
    last
};

namespace internal
{
    inline std::string enum_to_str(day_of_week_index i)
    {
        switch (i)
        {
        case day_of_week_index::first:
            return "First";
        case day_of_week_index::second:
            return "Second";
        case day_of_week_index::third:
            return "Third";
        case day_of_week_index::fourth:
            return "Fourth";
        case day_of_week_index::last:
            return "Last";
        default:
            throw exception("Unexpected <DayOfWeekIndex>");
        }
    }

    inline day_of_week_index str_to_day_of_week_index(const std::string& str)
    {
        if (str == "First")
        {
            return day_of_week_index::first;
        }
        else if (str == "Second")
        {
            return day_of_week_index::second;
        }
        else if (str == "Third")
        {
            return day_of_week_index::third;
        }
        else if (str == "Fourth")
        {
            return day_of_week_index::fourth;
        }
        else if (str == "Last")
        {
            return day_of_week_index::last;
        }
        else
        {
            throw exception("Unexpected <DayOfWeekIndex>");
        }
    }
} // namespace internal

//! Represents a concrete task in the Exchange store
class task final : public item
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    task() = default;
#else
    task() {}
#endif

    //! Constructs a new task with the given item_id
    explicit task(item_id id) : item(std::move(id)) {}

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
    task(item_id&& id, internal::xml_subtree&& properties)
        : item(std::move(id), std::move(properties))
    {
    }
#endif

    //! \brief Returns the actual amount of work expended on the task.
    //!
    //! Measured in minutes.
    int get_actual_work() const
    {
        const auto val = xml().get_value_as_string("ActualWork");
        if (val.empty())
        {
            return 0;
        }
        return std::stoi(val);
    }

    //! \brief Sets the actual amount of work expended on the task.
    //!
    //! Measured in minutes.
    void set_actual_work(int actual_work)
    {
        xml().set_or_update("ActualWork", std::to_string(actual_work));
    }

    //! \brief Returns the time this task was assigned to the current
    //! owner.
    //!
    //! If this task is not a delegated task, this property is not set.
    //! This is a read-only property.
    date_time get_assigned_time() const
    {
        return date_time(xml().get_value_as_string("AssignedTime"));
    }

    //! Returns the billing information associated with this task
    std::string get_billing_information() const
    {
        return xml().get_value_as_string("BillingInformation");
    }

    //! Sets the billing information associated with this task
    void set_billing_information(const std::string& billing_info)
    {
        xml().set_or_update("BillingInformation", billing_info);
    }

    //! \brief Returns the change count of this task.
    //!
    //! How many times this task has been acted upon (sent, accepted,
    //! etc.). This is simply a way to resolve conflicts when the
    //! delegator sends multiple updates. Also known as TaskVersion
    //! Seems to be read-only.
    int get_change_count() const
    {
        const auto val = xml().get_value_as_string("ChangeCount");
        if (val.empty())
        {
            return 0;
        }
        return std::stoi(val);
    }

    //! \brief Returns the companies associated with this task.
    //!
    //! A list of company names associated with this task.
    //!
    //! Note: It seems that Exchange server accepts only one \<String>
    //! element here, although it is an ArrayOfStringsType.
    std::vector<std::string> get_companies() const
    {
        return get_array_of_strings_helper("Companies");
    }

    //! \brief Sets the companies associated with this task.
    //!
    //! Note: It seems that Exchange server accepts only one \<String>
    //! element here, although it is an ArrayOfStringsType.
    void set_companies(const std::vector<std::string>& companies)
    {
        set_array_of_strings_helper(companies, "Companies");
    }

    //! Returns the time the task was completed
    date_time get_complete_date() const
    {
        return date_time(xml().get_value_as_string("CompleteDate"));
    }

    //! Returns a list of contacts associated with this task
    std::vector<std::string> get_contacts() const
    {
        return get_array_of_strings_helper("Contacts");
    }

    //! Sets the contacts associated with this task to \p contacts.
    void set_contacts(const std::vector<std::string>& contacts)
    {
        set_array_of_strings_helper(contacts, "Contacts");
    }

    //! \brief Returns the delegation state of this task
    //!
    //! This is a read-only property.
    delegation_state get_delegation_state() const
    {
        const auto& val = xml().get_value_as_string("DelegationState");

        if (val.empty() || val == "NoMatch")
        {
            return delegation_state::no_match;
        }
        else if (val == "OwnNew")
        {
            return delegation_state::own_new;
        }
        else if (val == "Owned")
        {
            return delegation_state::owned;
        }
        else if (val == "Accepted")
        {
            return delegation_state::accepted;
        }
        else if (val == "Declined")
        {
            return delegation_state::declined;
        }
        else if (val == "Max")
        {
            return delegation_state::max;
        }
        else
        {
            throw exception("Unexpected <DelegationState>");
        }
    }

    //! Returns the name of the user that delegated the task
    std::string get_delegator() const
    {
        return xml().get_value_as_string("Delegator");
    }

    //! Sets the date that the task is due
    void set_due_date(const date_time& due_date)
    {
        xml().set_or_update("DueDate", due_date.to_string());
    }

    //! Returns the date that the task is due
    date_time get_due_date() const
    {
        return date_time(xml().get_value_as_string("DueDate"));
    }

// TODO
// This is a read-only property.
#if 0
        int is_assignment_editable() const
        {
            // Possible values:
            // 0 The default for all task items
            // 1 A task request
            // 2 A task acceptance from a recipient of a task request
            // 3 A task declination from a recipient of a task request
            // 4 An update to a previous task request
            // 5 Not used
        }
#endif

    //! \brief True if the task is marked as complete.
    //!
    //! This is a read-only property. See also
    //! task_property_path::percent_complete
    bool is_complete() const
    {
        return xml().get_value_as_string("IsComplete") == "true";
    }

    //! True if the task is recurring
    bool is_recurring() const
    {
        return xml().get_value_as_string("IsRecurring") == "true";
    }

    //! \brief True if the task is a team task.
    //!
    //! This is a read-only property.
    bool is_team_task() const
    {
        return xml().get_value_as_string("IsTeamTask") == "true";
    }

    //! \brief Returns the mileage associated with the task
    //!
    //! Potentially used for reimbursement purposes
    std::string get_mileage() const
    {
        return xml().get_value_as_string("Mileage");
    }

    //! Sets the mileage associated with the task
    void set_mileage(const std::string& mileage)
    {
        xml().set_or_update("Mileage", mileage);
    }

// TODO: Not in AllProperties shape in EWS 2013, investigate
// The name of the user who owns the task.
// This is a read-only property
#if 0
        std::string get_owner() const
        {
            return xml().get_value_as_string("Owner");
        }
#endif

    //! \brief Returns the percentage of the task that has been completed.
    //!
    //! Valid values are 0-100.
    int get_percent_complete() const
    {
        const auto val = xml().get_value_as_string("PercentComplete");
        if (val.empty())
        {
            return 0;
        }
        return std::stoi(val);
    }

    //! \brief Sets the percentage of the task that has been completed.
    //!
    //! Valid values are 0-100. Note that setting \<PercentComplete> to 100
    //! has the same effect as setting a \<CompleteDate> or \<Status> to
    //! ews::status::completed.
    //!
    //! See MSDN for more on this.
    void set_percent_complete(int value)
    {
        xml().set_or_update("PercentComplete", std::to_string(value));
    }

    // Used for recurring tasks
    // TODO: get_recurrence

    //! Set the date that work on the task should start
    void set_start_date(const date_time& start_date)
    {
        xml().set_or_update("StartDate", start_date.to_string());
    }

    //! Returns the date that work on the task should start
    date_time get_start_date() const
    {
        return date_time(xml().get_value_as_string("StartDate"));
    }

    //! Returns the status of the task
    status get_status() const
    {
        const auto& val = xml().get_value_as_string("Status");
        if (val == "NotStarted")
        {
            return status::not_started;
        }
        else if (val == "InProgress")
        {
            return status::in_progress;
        }
        else if (val == "Completed")
        {
            return status::completed;
        }
        else if (val == "WaitingOnOthers")
        {
            return status::waiting_on_others;
        }
        else if (val == "Deferred")
        {
            return status::deferred;
        }
        else
        {
            throw exception("Unexpected <Status>");
        }
    }

    //! Sets the status of the task to \p s.
    void set_status(status s)
    {
        xml().set_or_update("Status", internal::enum_to_str(s));
    }

    //! \brief Returns the status description.
    //!
    //! A localized string version of the status. Useful for display
    //! purposes. This is a read-only property.
    std::string get_status_description() const
    {
        return xml().get_value_as_string("StatusDescription");
    }

    //! Returns the total amount of work for this task
    int get_total_work() const
    {
        const auto val = xml().get_value_as_string("TotalWork");
        if (val.empty())
        {
            return 0;
        }
        return std::stoi(val);
    }

    //! Sets the total amount of work for this task
    void set_total_work(int total_work)
    {
        xml().set_or_update("TotalWork", std::to_string(total_work));
    }

    // Every property below is 2010 or 2013 dialect

    // TODO: add remaining properties

    //! Makes a task instance from a <tt>\<Task></tt> XML element
    static task from_xml_element(const rapidxml::xml_node<>& elem)
    {
        auto id_node =
            elem.first_node_ns(internal::uri<>::microsoft::types(), "ItemId");
        check(id_node, "Expected <ItemId>");
        return task(item_id::from_xml_element(*id_node),
                    internal::xml_subtree(elem));
    }

private:
    template <typename U> friend class basic_service;

    const std::string& item_tag_name() const EWS_NOEXCEPT
    {
        static const std::string name("Task");
        return name;
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<task>::value);
static_assert(std::is_copy_constructible<task>::value);
static_assert(std::is_copy_assignable<task>::value);
static_assert(std::is_move_constructible<task>::value);
static_assert(std::is_move_assignable<task>::value);
#endif

class complete_name final
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    complete_name() = default;
#else
    complete_name() {}
#endif

    complete_name(std::string title, std::string firstname,
                  std::string middlename, std::string lastname,
                  std::string suffix, std::string initials,
                  std::string fullname, std::string nickname)
        : title_(std::move(title)), firstname_(std::move(firstname)),
          middlename_(std::move(middlename)), lastname_(std::move(lastname)),
          suffix_(std::move(suffix)), initials_(std::move(initials)),
          fullname_(std::move(fullname)), nickname_(std::move(nickname))
    {
    }

    static complete_name from_xml_element(const rapidxml::xml_node<char>& node)
    {
        using namespace rapidxml::internal;
        std::string title;
        std::string firstname;
        std::string middlename;
        std::string lastname;
        std::string suffix;
        std::string initials;
        std::string fullname;
        std::string nickname;
        for (auto child = node.first_node(); child != nullptr;
             child = child->next_sibling())
        {
            if (compare("Title", strlen("Title"), child->local_name(),
                        child->local_name_size()))
            {
                title = std::string(child->value(), child->value_size());
            }
            else if (compare("FirstName", strlen("FirstName"),
                             child->local_name(), child->local_name_size()))
            {
                firstname = std::string(child->value(), child->value_size());
            }
            else if (compare("MiddleName", strlen("MiddleName"),
                             child->local_name(), child->local_name_size()))
            {
                middlename = std::string(child->value(), child->value_size());
            }
            else if (compare("LastName", strlen("LastName"),
                             child->local_name(), child->local_name_size()))
            {
                lastname = std::string(child->value(), child->value_size());
            }
            else if (compare("Suffix", strlen("Suffix"), child->local_name(),
                             child->local_name_size()))
            {
                suffix = std::string(child->value(), child->value_size());
            }
            else if (compare("Initials", strlen("Initials"),
                             child->local_name(), child->local_name_size()))
            {
                initials = std::string(child->value(), child->value_size());
            }
            else if (compare("FullName", strlen("FullName"),
                             child->local_name(), child->local_name_size()))
            {
                fullname = std::string(child->value(), child->value_size());
            }
            else if (compare("Nickname", strlen("Nickname"),
                             child->local_name(), child->local_name_size()))
            {
                nickname = std::string(child->value(), child->value_size());
            }
        }

        return complete_name(title, firstname, middlename, lastname, suffix,
                             initials, fullname, nickname);
    }

    const std::string& get_title() const EWS_NOEXCEPT { return title_; }
    const std::string& get_first_name() const EWS_NOEXCEPT
    {
        return firstname_;
    }
    const std::string& get_middle_name() const EWS_NOEXCEPT
    {
        return middlename_;
    }
    const std::string& get_last_name() const EWS_NOEXCEPT { return lastname_; }
    const std::string& get_suffix() const EWS_NOEXCEPT { return suffix_; }
    const std::string& get_initials() const EWS_NOEXCEPT { return initials_; }
    const std::string& get_full_name() const EWS_NOEXCEPT { return fullname_; }
    const std::string& get_nickname() const EWS_NOEXCEPT { return nickname_; }

private:
    std::string title_;
    std::string firstname_;
    std::string middlename_;
    std::string lastname_;
    std::string suffix_;
    std::string initials_;
    std::string fullname_;
    std::string nickname_;
};

class email_address final
{
public:
    enum class key
    {
        email_address_1,
        email_address_2,
        email_address_3
    };

    explicit email_address(key k, std::string value)
        : key_(std::move(k)), value_(std::move(value))
    {
    }

    static email_address
    from_xml_element(const rapidxml::xml_node<char>& node); // defined below
    std::string to_xml() const;
    key get_key() const EWS_NOEXCEPT { return key_; }
    const std::string& get_value() const EWS_NOEXCEPT { return value_; }

private:
    key key_;
    std::string value_;
    friend bool operator==(const email_address&, const email_address&);
};

namespace internal
{
    inline email_address::key
    str_to_email_address_key(const std::string& keystring)
    {
        email_address::key k;
        if (keystring == "EmailAddress1")
        {
            k = email_address::key::email_address_1;
        }
        else if (keystring == "EmailAddress2")
        {
            k = email_address::key::email_address_2;
        }
        else if (keystring == "EmailAddress3")
        {
            k = email_address::key::email_address_3;
        }
        else
        {
            throw exception("Unrecognized key: " + keystring);
        }
        return k;
    }

    inline std::string enum_to_str(email_address::key k)
    {
        switch (k)
        {
        case email_address::key::email_address_1:
            return "EmailAddress1";
        case email_address::key::email_address_2:
            return "EmailAddress2";
        case email_address::key::email_address_3:
            return "EmailAddress3";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

inline email_address
email_address::from_xml_element(const rapidxml::xml_node<char>& node)
{
    using namespace internal;
    using rapidxml::internal::compare;

    // <t:EmailAddresses>
    //  <Entry Key="EmailAddress1">donald.duck@duckburg.de</Entry>
    //  <Entry Key="EmailAddress3">dragonmaster1999@yahoo.com</Entry>
    // </t:EmailAddresses>

    check(compare(node.local_name(), node.local_name_size(), "Entry",
                  strlen("Entry")),
          "Expected <Entry> element");
    auto key = node.first_attribute("Key");
    check(key, "Expected attribute 'Key'");
    return email_address(
        str_to_email_address_key(std::string(key->value(), key->value_size())),
        std::string(node.value(), node.value_size()));
}

inline std::string email_address::to_xml() const
{
    std::stringstream sstr;
    sstr << " <t:"
         << "EmailAddresses"
         << ">";
    sstr << " <t:Entry Key=";
    sstr << "\"" << internal::enum_to_str(get_key());
    sstr << "\">";
    sstr << get_value();
    sstr << "</t:Entry>";
    sstr << " </t:"
         << "EmailAddresses"
         << ">";
    return sstr.str();
}

inline bool operator==(const email_address& lhs, const email_address& rhs)
{
    return (lhs.key_ == rhs.key_) && (lhs.value_ == rhs.value_);
}

class physical_address final
{
public:
    enum class key
    {
        home,
        business,
        other
    };

    physical_address(key k, std::string street, std::string city,
                     std::string state, std::string cor,
                     std::string postal_code)
        : key_(std::move(k)), street_(std::move(street)),
          city_(std::move(city)), state_(std::move(state)),
          country_or_region_(std::move(cor)),
          postal_code_(std::move(postal_code))
    {
    }

    static physical_address
    from_xml_element(const rapidxml::xml_node<char>& node); // defined below
    std::string to_xml() const;
    key get_key() const EWS_NOEXCEPT { return key_; }
    const std::string& street() const EWS_NOEXCEPT { return street_; }
    const std::string& city() const EWS_NOEXCEPT { return city_; }
    const std::string& state() const EWS_NOEXCEPT { return state_; }
    const std::string& country_or_region() const EWS_NOEXCEPT
    {
        return country_or_region_;
    }
    const std::string& postal_code() const EWS_NOEXCEPT { return postal_code_; }

private:
    key key_;
    std::string street_;
    std::string city_;
    std::string state_;
    std::string country_or_region_;
    std::string postal_code_;

    friend bool operator==(const physical_address&, const physical_address&);
};

inline bool operator==(const physical_address& lhs, const physical_address& rhs)
{
    return (lhs.key_ == rhs.key_) && (lhs.street_ == rhs.street_) &&
           (lhs.city_ == rhs.city_) && (lhs.state_ == rhs.state_) &&
           (lhs.country_or_region_ == rhs.country_or_region_) &&
           (lhs.postal_code_ == rhs.postal_code_);
}

namespace internal
{
    inline physical_address::key
    string_to_physical_address_key(const std::string& keystring)
    {
        physical_address::key k;
        if (keystring == "Home")
        {
            k = physical_address::key::home;
        }
        else if (keystring == "Business")
        {
            k = physical_address::key::business;
        }
        else if (keystring == "Other")
        {
            k = physical_address::key::other;
        }
        else
        {
            throw exception("Unrecognized key: " + keystring);
        }
        return k;
    }

    inline std::string enum_to_str(physical_address::key k)
    {
        switch (k)
        {
        case physical_address::key::home:
            return "Home";
        case physical_address::key::business:
            return "Business";
        case physical_address::key::other:
            return "Other";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

inline physical_address
physical_address::from_xml_element(const rapidxml::xml_node<char>& node)
{
    using namespace internal;
    using rapidxml::internal::compare;

    check(compare(node.local_name(), node.local_name_size(), "Entry",
                  strlen("Entry")),
          "Expected <Entry>, got something else");

    // <Entry Key="Home">
    //      <Street>
    //      <City>
    //      <State>
    //      <CountryOrRegion>
    //      <PostalCode>
    // </Entry>

    auto key_attr = node.first_attribute();
    check(key_attr, "Expected <Entry> to have an attribute");
    check(compare(key_attr->name(), key_attr->name_size(), "Key", 3),
          "Expected <Entry> to have an attribute 'Key'");
    const key key = string_to_physical_address_key(key_attr->value());

    std::string street;
    std::string city;
    std::string state;
    std::string cor;
    std::string postal_code;

    for (auto child = node.first_node(); child != nullptr;
         child = child->next_sibling())
    {
        if (compare("Street", strlen("Street"), child->local_name(),
                    child->local_name_size()))
        {
            street = std::string(child->value(), child->value_size());
        }
        if (compare("City", strlen("City"), child->local_name(),
                    child->local_name_size()))
        {
            city = std::string(child->value(), child->value_size());
        }
        if (compare("State", strlen("State"), child->local_name(),
                    child->local_name_size()))
        {
            state = std::string(child->value(), child->value_size());
        }
        if (compare("CountryOrRegion", strlen("CountryOrRegion"),
                    child->local_name(), child->local_name_size()))
        {
            cor = std::string(child->value(), child->value_size());
        }
        if (compare("PostalCode", strlen("PostalCode"), child->local_name(),
                    child->local_name_size()))
        {
            postal_code = std::string(child->value(), child->value_size());
        }
    }
    return physical_address(key, street, city, state, cor, postal_code);
}

inline std::string physical_address::to_xml() const
{
    std::stringstream sstr;
    sstr << " <t:"
         << "PhysicalAddresses"
         << ">";
    sstr << " <t:Entry Key=";
    sstr << "\"" << internal::enum_to_str(get_key());
    sstr << "\">";
    if (!street().empty())
    {
        sstr << "<t:Street>";
        sstr << street();
        sstr << "</t:Street>";
    }
    if (!city().empty())
    {
        sstr << "<t:City>";
        sstr << city();
        sstr << "</t:City>";
    }
    if (!state().empty())
    {
        sstr << "<t:State>";
        sstr << state();
        sstr << "</t:State>";
    }
    if (!country_or_region().empty())
    {
        sstr << "<t:CountryOrRegion>";
        sstr << country_or_region();
        sstr << "</t:CountryOrRegion>";
    }
    if (!postal_code().empty())
    {
        sstr << "<t:PostalCode>";
        sstr << postal_code();
        sstr << "</t:PostalCode>";
    }
    sstr << "</t:Entry>";
    sstr << " </t:"
         << "PhysicalAddresses"
         << ">";
    return sstr.str();
}

namespace internal
{
    enum file_as_mapping
    {
        none,
        last_comma_first,
        first_space_last,
        company,
        last_comma_first_company,
        company_last_first,
        last_first,
        last_first_company,
        company_last_comma_first,
        last_first_suffix,
        last_space_first_company,
        company_last_space_first,
        last_space_first
    };

    inline file_as_mapping str_to_map(const std::string& maptype)
    {
        file_as_mapping map;

        if (maptype == "LastCommaFirst")
        {
            map = file_as_mapping::last_comma_first;
        }
        else if (maptype == "FirstSpaceLast")
        {
            map = file_as_mapping::first_space_last;
        }
        else if (maptype == "Company")
        {
            map = file_as_mapping::company;
        }
        else if (maptype == "LastCommaFirstCompany")
        {
            map = file_as_mapping::last_comma_first_company;
        }
        else if (maptype == "CompanyLastFirst")
        {
            map = file_as_mapping::company_last_first;
        }
        else if (maptype == "LastFirst")
        {
            map = file_as_mapping::last_first;
        }
        else if (maptype == "LastFirstCompany")
        {
            map = file_as_mapping::last_first_company;
        }
        else if (maptype == "CompanyLastCommaFirst")
        {
            map = file_as_mapping::company_last_comma_first;
        }
        else if (maptype == "LastFirstSuffix")
        {
            map = file_as_mapping::last_first_suffix;
        }
        else if (maptype == "LastSpaceFirstCompany")
        {
            map = file_as_mapping::last_space_first_company;
        }
        else if (maptype == "CompanyLastSpaceFirst")
        {
            map = file_as_mapping::company_last_space_first;
        }
        else if (maptype == "LastSpaceFirst")
        {
            map = file_as_mapping::last_space_first;
        }
        else if (maptype == "None" || maptype == "")
        {
            map = file_as_mapping::none;
        }
        else
        {
            throw exception(std::string("Unrecognized FileAsMapping Type: ") +
                            maptype);
        }
        return map;
    }

    inline std::string enum_to_str(internal::file_as_mapping maptype)
    {
        std::string mappingtype;
        switch (maptype)
        {
        case file_as_mapping::none:
            mappingtype = "None";
            break;
        case file_as_mapping::last_comma_first:
            mappingtype = "LastCommaFirst";
            break;
        case file_as_mapping::first_space_last:
            mappingtype = "FirstSpaceLast";
            break;
        case file_as_mapping::company:
            mappingtype = "Company";
            break;
        case file_as_mapping::last_comma_first_company:
            mappingtype = "LastCommaFirstCompany";
            break;
        case file_as_mapping::company_last_first:
            mappingtype = "CompanyLastFirst";
            break;
        case file_as_mapping::last_first:
            mappingtype = "LastFirst";
            break;
        case file_as_mapping::last_first_company:
            mappingtype = "LastFirstCompany";
            break;
        case file_as_mapping::company_last_comma_first:
            mappingtype = "CompanyLastCommaFirst";
            break;
        case file_as_mapping::last_first_suffix:
            mappingtype = "LastFirstSuffix";
            break;
        case file_as_mapping::last_space_first_company:
            mappingtype = "LastSpaceFirstCompany";
            break;
        case file_as_mapping::company_last_space_first:
            mappingtype = "CompanyLastSpaceFirst";
            break;
        case file_as_mapping::last_space_first:
            mappingtype = "LastSpaceFirst";
            break;
        default:
            break;
        }
        return mappingtype;
    }
} // namespace internal

class im_address final
{
public:
    enum class key
    {
        imaddress1,
        imaddress2,
        imaddress3
    };

    im_address(key k, std::string value)
        : key_(std::move(k)), value_(std::move(value))
    {
    }

    static im_address from_xml_element(const rapidxml::xml_node<char>& node);
    std::string to_xml() const;

    key get_key() const EWS_NOEXCEPT { return key_; }
    const std::string& get_value() const EWS_NOEXCEPT { return value_; }

private:
    key key_;
    std::string value_;
    friend bool operator==(const im_address&, const im_address&);
};

namespace internal
{
    inline std::string enum_to_str(im_address::key k)
    {
        switch (k)
        {
        case im_address::key::imaddress1:
            return "ImAddress1";
        case im_address::key::imaddress2:
            return "ImAddress2";
        case im_address::key::imaddress3:
            return "ImAddress3";
        default:
            throw exception("Bad enum value");
        }
    }

    inline im_address::key str_to_im_address_key(const std::string& keystring)
    {
        im_address::key k;
        if (keystring == "ImAddress1")
        {
            k = im_address::key::imaddress1;
        }
        else if (keystring == "ImAddress2")
        {
            k = im_address::key::imaddress2;
        }
        else if (keystring == "ImAddress3")
        {
            k = im_address::key::imaddress3;
        }
        else
        {
            throw exception("Unrecognized key: " + keystring);
        }
        return k;
    }
} // namespace internal

inline im_address
im_address::from_xml_element(const rapidxml::xml_node<char>& node)
{
    using namespace internal;
    using rapidxml::internal::compare;

    check(compare(node.local_name(), node.local_name_size(), "Entry",
                  strlen("Entry")),
          "Expected <Entry>, got something else");
    auto key = node.first_attribute("Key");
    check(key, "Expected attribute 'Key'");
    return im_address(
        str_to_im_address_key(std::string(key->value(), key->value_size())),
        std::string(node.value(), node.value_size()));
}

inline std::string im_address::to_xml() const
{
    std::stringstream sstr;
    sstr << " <t:"
         << "ImAddresses"
         << ">";
    sstr << " <t:Entry Key=";
    sstr << "\"" << internal::enum_to_str(key_);
    sstr << "\">";
    sstr << get_value();
    sstr << "</t:Entry>";
    sstr << " </t:"
         << "ImAddresses"
         << ">";
    return sstr.str();
}

inline bool operator==(const im_address& lhs, const im_address& rhs)
{
    return (lhs.key_ == rhs.key_) && (lhs.value_ == rhs.value_);
}

class phone_number final
{
public:
    enum class key
    {
        assistant_phone,
        business_fax,
        business_phone,
        business_phone_2,
        callback,
        car_phone,
        company_main_phone,
        home_fax,
        home_phone,
        home_phone_2,
        isdn,
        mobile_phone,
        other_fax,
        other_telephone,
        pager,
        primary_phone,
        radio_phone,
        telex,
        ttytdd_phone
    };

    phone_number(key k, std::string val)
        : key_(std::move(k)), value_(std::move(val))
    {
    }

    static phone_number from_xml_element(const rapidxml::xml_node<char>& node);
    std::string to_xml() const;
    key get_key() const EWS_NOEXCEPT { return key_; }
    const std::string& get_value() const EWS_NOEXCEPT { return value_; }

private:
    key key_;
    std::string value_;
    friend bool operator==(const phone_number&, const phone_number&);
};

namespace internal
{
    inline phone_number::key
    str_to_phone_number_key(const std::string& keystring)
    {
        phone_number::key k;
        if (keystring == "AssistantPhone")
        {
            k = phone_number::key::assistant_phone;
        }
        else if (keystring == "BusinessFax")
        {
            k = phone_number::key::business_fax;
        }
        else if (keystring == "BusinessPhone")
        {
            k = phone_number::key::business_phone;
        }
        else if (keystring == "BusinessPhone2")
        {
            k = phone_number::key::business_phone_2;
        }
        else if (keystring == "Callback")
        {
            k = phone_number::key::callback;
        }
        else if (keystring == "CarPhone")
        {
            k = phone_number::key::car_phone;
        }
        else if (keystring == "CompanyMainPhone")
        {
            k = phone_number::key::company_main_phone;
        }
        else if (keystring == "HomeFax")
        {
            k = phone_number::key::home_fax;
        }
        else if (keystring == "HomePhone")
        {
            k = phone_number::key::home_phone;
        }
        else if (keystring == "HomePhone2")
        {
            k = phone_number::key::home_phone_2;
        }
        else if (keystring == "Isdn")
        {
            k = phone_number::key::isdn;
        }
        else if (keystring == "MobilePhone")
        {
            k = phone_number::key::mobile_phone;
        }
        else if (keystring == "OtherFax")
        {
            k = phone_number::key::other_fax;
        }
        else if (keystring == "OtherTelephone")
        {
            k = phone_number::key::other_telephone;
        }
        else if (keystring == "Pager")
        {
            k = phone_number::key::pager;
        }
        else if (keystring == "PrimaryPhone")
        {
            k = phone_number::key::primary_phone;
        }
        else if (keystring == "RadioPhone")
        {
            k = phone_number::key::radio_phone;
        }
        else if (keystring == "Telex")
        {
            k = phone_number::key::telex;
        }
        else if (keystring == "TtyTddPhone")
        {
            k = phone_number::key::ttytdd_phone;
        }
        else
        {
            throw exception("Unrecognized key: " + keystring);
        }
        return k;
    }

    inline std::string enum_to_str(phone_number::key k)
    {
        switch (k)
        {
        case phone_number::key::assistant_phone:
            return "AssistantPhone";
        case phone_number::key::business_fax:
            return "BusinessFax";
        case phone_number::key::business_phone:
            return "BusinessPhone";
        case phone_number::key::business_phone_2:
            return "BusinessPhone2";
        case phone_number::key::callback:
            return "Callback";
        case phone_number::key::car_phone:
            return "CarPhone";
        case phone_number::key::company_main_phone:
            return "CompanyMainPhone";
        case phone_number::key::home_fax:
            return "HomeFax";
        case phone_number::key::home_phone:
            return "HomePhone";
        case phone_number::key::home_phone_2:
            return "HomePhone2";
        case phone_number::key::isdn:
            return "Isdn";
        case phone_number::key::mobile_phone:
            return "MobilePhone";
        case phone_number::key::other_fax:
            return "OtherFax";
        case phone_number::key::other_telephone:
            return "OtherTelephone";
        case phone_number::key::pager:
            return "Pager";
        case phone_number::key::primary_phone:
            return "PrimaryPhone";
        case phone_number::key::radio_phone:
            return "RadioPhone";
        case phone_number::key::telex:
            return "Telex";
        case phone_number::key::ttytdd_phone:
            return "TtyTddPhone";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

inline phone_number
phone_number::from_xml_element(const rapidxml::xml_node<char>& node)
{
    using namespace internal;
    using rapidxml::internal::compare;

    // <t:PhoneNumbers>
    //  <Entry Key="AssistantPhone">0123456789</Entry>
    //  <Entry Key="BusinessFax">9876543210</Entry>
    // </t:PhoneNumbers>

    check(compare(node.local_name(), node.local_name_size(), "Entry",
                  strlen("Entry")),
          "Expected <Entry>, got something else");
    auto key = node.first_attribute("Key");
    check(key, "Expected attribute 'Key'");
    return phone_number(
        str_to_phone_number_key(std::string(key->value(), key->value_size())),
        std::string(node.value(), node.value_size()));
}

inline std::string phone_number::to_xml() const
{
    std::stringstream sstr;
    sstr << " <t:"
         << "PhoneNumbers"
         << ">";
    sstr << " <t:Entry Key=";
    sstr << "\"" << internal::enum_to_str(key_);
    sstr << "\">";
    sstr << get_value();
    sstr << "</t:Entry>";
    sstr << " </t:"
         << "PhoneNumbers"
         << ">";
    return sstr.str();
}

inline bool operator==(const phone_number& lhs, const phone_number& rhs)
{
    return (lhs.key_ == rhs.key_) && (lhs.value_ == rhs.value_);
}

//! A contact item in the Exchange store.
class contact final : public item
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    contact() = default;
#else
    contact() {}
#endif

    explicit contact(item_id id) : item(id) {}

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
    contact(item_id&& id, internal::xml_subtree&& properties)
        : item(std::move(id), std::move(properties))
    {
    }
#endif

    //! How the name should be filed for display/sorting purposes
    void set_file_as(const std::string& fileas)
    {
        xml().set_or_update("FileAs", fileas);
    }

    std::string get_file_as() const
    {
        return xml().get_value_as_string("FileAs");
    }

    //! How the various parts of a contact's information interact to form
    //! the FileAs property value
    //! Overrides previously made FileAs settings
    void set_file_as_mapping(internal::file_as_mapping maptype)
    {
        auto mapping = internal::enum_to_str(maptype);
        xml().set_or_update("FileAsMapping", mapping);
    }

    internal::file_as_mapping get_file_as_mapping() const
    {
        return internal::str_to_map(xml().get_value_as_string("FileAsMapping"));
    }

    //! Sets the name to display for a contact
    void set_display_name(const std::string& display_name)
    {
        xml().set_or_update("DisplayName", display_name);
    }

    //! Returns the displayed name of the contact
    std::string get_display_name() const
    {
        return xml().get_value_as_string("DisplayName");
    }

    //! Sets the name by which a person is known to `given_name`; often
    //! referred to as a person's first name
    void set_given_name(const std::string& given_name)
    {
        xml().set_or_update("GivenName", given_name);
    }

    //! Returns the person's first name
    std::string get_given_name() const
    {
        return xml().get_value_as_string("GivenName");
    }

    //! Set the Initials for the contact
    void set_initials(const std::string& initials)
    {
        xml().set_or_update("Initials", initials);
    }

    //! Returns the person's initials
    std::string get_initials() const
    {
        return xml().get_value_as_string("Initials");
    }

    //! Set the middle name for the contact
    void set_middle_name(const std::string& middle_name)
    {
        xml().set_or_update("MiddleName", middle_name);
    }

    //! Returns the middle name of the contact
    std::string get_middle_name() const
    {
        return xml().get_value_as_string("MiddleName");
    }

    //! Sets another name by which the contact is known
    void set_nickname(const std::string& nickname)
    {
        xml().set_or_update("Nickname", nickname);
    }

    //! Returns the nickname of the contact
    std::string get_nickname() const
    {
        return xml().get_value_as_string("Nickname");
    }

    //! A combination of several name fields in one convenient place
    complete_name get_complete_name() const
    {
        auto node = xml().get_node("CompleteName");
        if (node == nullptr)
        {
            return complete_name();
        }
        return complete_name::from_xml_element(*node);
    }

    //! Sets the company that the contact is affiliated with
    void set_company_name(const std::string& company_name)
    {
        xml().set_or_update("CompanyName", company_name);
    }

    //! Returns the comany of the contact
    std::string get_company_name() const
    {
        return xml().get_value_as_string("CompanyName");
    }

    //! A collection of email addresses for the contact
    std::vector<email_address> get_email_addresses() const
    {
        const auto addresses = xml().get_node("EmailAddresses");
        if (!addresses)
        {
            return std::vector<email_address>();
        }
        std::vector<email_address> result;
        for (auto entry = addresses->first_node(); entry != nullptr;
             entry = entry->next_sibling())
        {
            result.push_back(email_address::from_xml_element(*entry));
        }
        return result;
    }

    void set_email_address(const email_address& address)
    {
        using internal::create_node;
        using rapidxml::internal::compare;
        auto doc = xml().document();
        auto email_addresses = xml().get_node("EmailAddresses");

        if (email_addresses)
        {
            bool entry_exists = false;
            auto entry = email_addresses->first_node();
            for (; entry != nullptr; entry = entry->next_sibling())
            {
                auto key_attr = entry->first_attribute();
                check(key_attr, "Expected an attribute");
                check(
                    compare(key_attr->name(), key_attr->name_size(), "Key", 3),
                    "Expected an attribute 'Key'");
                const auto key = internal::enum_to_str(address.get_key());
                if (compare(key_attr->value(), key_attr->value_size(),
                            key.c_str(), key.size()))
                {
                    entry_exists = true;
                    break;
                }
            }
            if (entry_exists)
            {
                email_addresses->remove_node(entry);
            }
        }
        else
        {
            email_addresses = &create_node(*doc, "t:EmailAddresses");
        }

        // create entry & key
        const auto value = address.get_value();
        auto new_entry = &create_node(*email_addresses, "t:Entry", value);
        auto ptr_to_key = doc->allocate_string("Key");
        const auto key = internal::enum_to_str(address.get_key());
        auto ptr_to_value = doc->allocate_string(key.c_str());
        new_entry->append_attribute(
            doc->allocate_attribute(ptr_to_key, ptr_to_value));
    }

    //! A collection of mailing addresses for the contact
    std::vector<physical_address> get_physical_addresses() const
    {
        const auto addresses = xml().get_node("PhysicalAddresses");
        if (!addresses)
        {
            return std::vector<physical_address>();
        }
        std::vector<physical_address> result;
        for (auto entry = addresses->first_node(); entry != nullptr;
             entry = entry->next_sibling())
        {
            result.push_back(physical_address::from_xml_element(*entry));
        }
        return result;
    }

    void set_physical_address(const physical_address& address)
    {
        using internal::create_node;
        using rapidxml::internal::compare;
        auto doc = xml().document();
        auto addresses = xml().get_node("PhysicalAddresses");
        // <PhysicalAddresses>
        //   <Entry Key="Home">
        //     <Street>
        //     <City>
        //     <State>
        //     <CountryOrRegion>
        //     <PostalCode>
        //   <Entry/>
        //   <Entry Key="Business">
        //     <Street>
        //     <City>
        //     <State>
        //     <CountryOrRegion>
        //     <PostalCode>
        //   <Entry/>
        // <PhysicalAddresses/>

        if (addresses)
        {
            bool entry_exists = false;
            auto entry = addresses->first_node();
            for (; entry != nullptr; entry = entry->next_sibling())
            {
                auto key_attr = entry->first_attribute();
                check(key_attr, "Expected an attribute");
                check(
                    compare(key_attr->name(), key_attr->name_size(), "Key", 3),
                    "Expected an attribute 'Key'");
                const auto key = internal::enum_to_str(address.get_key());
                if (compare(key_attr->value(), key_attr->value_size(),
                            key.c_str(), key.size()))
                {
                    entry_exists = true;
                    break;
                }
            }
            if (entry_exists)
            {
                addresses->remove_node(entry);
            }
        }
        else
        {
            addresses = &create_node(*doc, "t:PhysicalAddresses");
        }

        // create entry & key
        auto new_entry = &create_node(*addresses, "t:Entry");

        auto ptr_to_key = doc->allocate_string("Key");
        auto keystr = internal::enum_to_str(address.get_key());
        auto ptr_to_value = doc->allocate_string(keystr.c_str());
        new_entry->append_attribute(
            doc->allocate_attribute(ptr_to_key, ptr_to_value));

        if (!address.street().empty())
        {
            create_node(*new_entry, "t:Street", address.street());
        }
        if (!address.city().empty())
        {
            create_node(*new_entry, "t:City", address.city());
        }
        if (!address.state().empty())
        {
            create_node(*new_entry, "t:State", address.state());
        }
        if (!address.country_or_region().empty())
        {
            create_node(*new_entry, "t:CountryOrRegion",
                        address.country_or_region());
        }
        if (!address.postal_code().empty())
        {
            create_node(*new_entry, "t:PostalCode", address.postal_code());
        }
    }

    // A collection of phone numbers for the contact
    void set_phone_number(const phone_number& number)
    {
        using internal::create_node;
        using rapidxml::internal::compare;
        auto doc = xml().document();
        auto phone_numbers = xml().get_node("PhoneNumbers");

        if (phone_numbers)
        {
            bool entry_exists = false;
            auto entry = phone_numbers->first_node();
            for (; entry != nullptr; entry = entry->next_sibling())
            {
                auto key_attr = entry->first_attribute();
                check(key_attr, "Expected an attribute");
                check(
                    compare(key_attr->name(), key_attr->name_size(), "Key", 3),
                    "Expected an attribute 'Key'");
                const auto key = internal::enum_to_str(number.get_key());
                if (compare(key_attr->value(), key_attr->value_size(),
                            key.c_str(), key.size()))
                {
                    entry_exists = true;
                    break;
                }
            }
            if (entry_exists)
            {
                phone_numbers->remove_node(entry);
            }
        }
        else
        {
            phone_numbers = &create_node(*doc, "t:PhoneNumbers");
        }

        // create entry & key
        const auto value = number.get_value();
        auto new_entry = &create_node(*phone_numbers, "t:Entry", value);
        auto ptr_to_key = doc->allocate_string("Key");
        const auto key = internal::enum_to_str(number.get_key());
        auto ptr_to_value = doc->allocate_string(key.c_str());
        new_entry->append_attribute(
            doc->allocate_attribute(ptr_to_key, ptr_to_value));
    }

    std::vector<phone_number> get_phone_numbers() const
    {
        const auto numbers = xml().get_node("PhoneNumbers");
        if (!numbers)
        {
            return std::vector<phone_number>();
        }
        std::vector<phone_number> result;
        for (auto entry = numbers->first_node(); entry != nullptr;
             entry = entry->next_sibling())
        {
            result.push_back(phone_number::from_xml_element(*entry));
        }
        return result;
    }

    //! Sets the name of the contact's assistant
    void set_assistant_name(const std::string& assistant_name)
    {
        xml().set_or_update("AssistantName", assistant_name);
    }

    //! Returns the contact's assistant's name
    std::string get_assistant_name() const
    {
        return xml().get_value_as_string("AssistantName");
    }

    //! The contact's birthday
    // Be careful with the formating of the date string
    // It has to be in the format YYYY-MM-DD(THH:MM:SSZ) <- can be left out
    // if the time of the day isn't important, will automatically be set to
    // YYYY-MM-DDT00:00:00Z
    //
    // This also applies to any other contact property with a date type
    // string
    void set_birthday(const std::string& birthday)
    {
        xml().set_or_update("Birthday", birthday);
    }

    std::string get_birthday() const
    {
        return xml().get_value_as_string("Birthday");
    }

    //! Sets the web page for the contact's business; typically a URL
    void set_business_homepage(const std::string& business_homepage)
    {
        xml().set_or_update("BusinessHomePage", business_homepage);
    }

    //! Returns the URL of the contact
    std::string get_business_homepage() const
    {
        return xml().get_value_as_string("BusinessHomePage");
    }

    //! A collection of children's names associated with the contact
    void set_children(const std::vector<std::string>& children)
    {
        set_array_of_strings_helper(children, "Children");
    }

    std::vector<std::string> get_children() const
    {
        return get_array_of_strings_helper("Children");
    }

    //! A collection of companies a contact is associated with
    void set_companies(const std::vector<std::string>& companies)
    {
        set_array_of_strings_helper(companies, "Companies");
    }

    std::vector<std::string> get_companies() const
    {
        return get_array_of_strings_helper("Companies");
    }

    //! \brief Indicates whether this is a directory or a store contact.
    //!
    //! This is a read-only property.
    std::string get_contact_source() const
    {
        return xml().get_value_as_string("ContactSource");
    }

    //! Set the department name that the contact is in
    void set_department(const std::string& department)
    {
        xml().set_or_update("Department", department);
    }

    //! Return the department name of the contact
    std::string get_department() const
    {
        return xml().get_value_as_string("Department");
    }

    //! Sets the generation of the contact
    //! e.g.: Sr, Jr, I, II, III, and so on
    void set_generation(const std::string& generation)
    {
        xml().set_or_update("Generation", generation);
    }

    //! Returns the generation of the contact
    std::string get_generation() const
    {
        return xml().get_value_as_string("Generation");
    }

    //! A collection of instant messaging addresses for the contact

    void set_im_address(const im_address& im_address)
    {
        using internal::create_node;
        using rapidxml::internal::compare;
        auto doc = xml().document();
        auto im_addresses = xml().get_node("ImAddresses");

        if (im_addresses)
        {
            bool entry_exists = false;
            auto entry = im_addresses->first_node();
            for (; entry != nullptr; entry = entry->next_sibling())
            {
                auto key_attr = entry->first_attribute();
                check(key_attr, "Expected an attribute");
                check(
                    compare(key_attr->name(), key_attr->name_size(), "Key", 3),
                    "Expected an attribute 'Key'");
                const auto key = internal::enum_to_str(im_address.get_key());
                if (compare(key_attr->value(), key_attr->value_size(),
                            key.c_str(), key.size()))
                {
                    entry_exists = true;
                    break;
                }
            }
            if (entry_exists)
            {
                im_addresses->remove_node(entry);
            }
        }
        else
        {
            im_addresses = &create_node(*doc, "t:ImAddresses");
        }

        // create entry & key
        const auto value = im_address.get_value();
        auto new_entry = &create_node(*im_addresses, "t:Entry", value);
        auto ptr_to_key = doc->allocate_string("Key");
        const auto key = internal::enum_to_str(im_address.get_key());
        auto ptr_to_value = doc->allocate_string(key.c_str());
        new_entry->append_attribute(
            doc->allocate_attribute(ptr_to_key, ptr_to_value));
    }

    std::vector<im_address> get_im_addresses() const
    {
        const auto addresses = xml().get_node("ImAddresses");
        if (!addresses)
        {
            return std::vector<im_address>();
        }
        std::vector<im_address> result;
        for (auto entry = addresses->first_node(); entry != nullptr;
             entry = entry->next_sibling())
        {
            result.push_back(im_address::from_xml_element(*entry));
        }
        return result;
    }

    //! Sets this contact's job title.
    void set_job_title(const std::string& title)
    {
        xml().set_or_update("JobTitle", title);
    }

    //! Returns the job title for the contact
    std::string get_job_title() const
    {
        return xml().get_value_as_string("JobTitle");
    }

    //! Sets the name of the contact's manager
    void set_manager(const std::string& manager)
    {
        xml().set_or_update("Manager", manager);
    }

    //! Returns the name of the contact's manager
    std::string get_manager() const
    {
        return xml().get_value_as_string("Manager");
    }

    //! Sets the distance that the contact resides
    //! from some reference point
    void set_mileage(const std::string& mileage)
    {
        xml().set_or_update("Mileage", mileage);
    }

    //! Returns the distance to the reference point
    std::string get_mileage() const
    {
        return xml().get_value_as_string("Mileage");
    }

    //! Sets the location of the contact's office
    void set_office_location(const std::string& office_location)
    {
        xml().set_or_update("OfficeLocation", office_location);
    }

    //! Returns the location of the contact's office
    std::string get_office_location() const
    {
        return xml().get_value_as_string("OfficeLocation");
    }

    // The physical addresses in the PhysicalAddresses collection that
    // represents the mailing address for the contact
    // TODO: get_postal_address_index

    //! Sets the occupation or discipline of the contact
    void set_profession(const std::string& profession)
    {
        xml().set_or_update("Profession", profession);
    }

    //! Returns the occupation of the contact
    std::string get_profession() const
    {
        return xml().get_value_as_string("Profession");
    }

    //! Set name of the contact's significant other
    void set_spouse_name(const std::string& spouse_name)
    {
        xml().set_or_update("SpouseName", spouse_name);
    }

    //! Get name of the contact's significant other
    std::string get_spouse_name() const
    {
        return xml().get_value_as_string("SpouseName");
    }

    //! Sets the family name of the contact; usually considered the last
    //! name
    void set_surname(const std::string& surname)
    {
        xml().set_or_update("Surname", surname);
    }

    //! Returns the family name of the contact; usually considered the last
    //! name
    std::string get_surname() const
    {
        return xml().get_value_as_string("Surname");
    }

    //! Date that the contact was married
    void set_wedding_anniversary(const std::string& anniversary)
    {
        xml().set_or_update("WeddingAnniversary", anniversary);
    }

    std::string get_wedding_anniversary() const
    {
        return xml().get_value_as_string("WeddingAnniversary");
    }

    // Everything below is beyond EWS 2007 subset

    // has_picture
    // phonetic_full_name
    // phonetic_first_name
    // phonetic_last_name
    // alias
    // notes
    // photo
    // user_smime_certificate
    // msexchange_certificate
    // directory_id
    // manager_mailbox
    // direct_reports

    std::string get_user_smime_certificate() const
    {
        const auto node = xml().get_node("UserSMIMECertificate");
        if (!node)
        {
            return "";
        }
        auto base64binary = node->first_node_ns(
            internal::uri<>::microsoft::types(), "Base64Binary");
        return base64binary ? std::string(base64binary->value(),
                                          base64binary->value_size())
                            : "";
    }

    std::string get_msexchange_certificate() const
    {
        const auto node = xml().get_node("MSExchangeCertificate");
        if (!node)
        {
            return "";
        }
        auto base64binary = node->first_node_ns(
            internal::uri<>::microsoft::types(), "Base64Binary");
        return base64binary ? std::string(base64binary->value(),
                                          base64binary->value_size())
                            : "";
    }

    //! Makes a contact instance from a \<Contact> XML element
    static contact from_xml_element(const rapidxml::xml_node<>& elem)
    {
        auto id_node =
            elem.first_node_ns(internal::uri<>::microsoft::types(), "ItemId");
        check(id_node, "Expected <ItemId>");
        return contact(item_id::from_xml_element(*id_node),
                       internal::xml_subtree(elem));
    }

    //! Makes a contact instance from a \<Contact> XML element that has no
    //! ItemId. Is the case in resolution sets.
    static contact
    from_xml_element_without_item_id(const rapidxml::xml_node<>& elem)
    {
        item_id id;
        return contact(std::move(id), internal::xml_subtree(elem));
    }

private:
    template <typename U> friend class basic_service;

    const std::string& item_tag_name() const EWS_NOEXCEPT
    {
        static const std::string name("Contact");
        return name;
    }

    // Helper function for get_email_address_{1,2,3}
    std::string get_email_address_by_key(const char* key) const
    {
        using rapidxml::internal::compare;

        // <Entry Key="" Name="" RoutingType="" MailboxType="" />
        const auto addresses = xml().get_node("EmailAddresses");
        if (!addresses)
        {
            return "";
        }
        for (auto entry = addresses->first_node(); entry;
             entry = entry->next_sibling())
        {
            for (auto attr = entry->first_attribute(); attr;
                 attr = attr->next_attribute())
            {
                if (compare(attr->name(), attr->name_size(), "Key", 3) &&
                    compare(attr->value(), attr->value_size(), key,
                            strlen(key)))
                {
                    return std::string(entry->value(), entry->value_size());
                }
            }
        }
        // None with such key
        return "";
    }

    // Helper function for set_email_address_{1,2,3}
    void set_email_address_by_key(const char* key, mailbox&& mail)
    {
        using internal::create_node;
        using rapidxml::internal::compare;

        auto doc = xml().document();
        auto addresses = xml().get_node("EmailAddresses");
        if (addresses)
        {
            // Check if there is already any entry for given key

            bool exists = false;
            auto entry = addresses->first_node();
            for (; entry && !exists; entry = entry->next_sibling())
            {
                for (auto attr = entry->first_attribute(); attr;
                     attr = attr->next_attribute())
                {
                    if (compare(attr->name(), attr->name_size(), "Key", 3) &&
                        compare(attr->value(), attr->value_size(), key,
                                strlen(key)))
                    {
                        exists = true;
                        break;
                    }
                }
            }
            if (exists)
            {
                addresses->remove_node(entry);
            }
        }
        else
        {
            // Need to construct <EmailAddresses> node first
            addresses = &create_node(*doc, "t:EmailAddresses");
        }

        // <Entry Key="" Name="" RoutingType="" MailboxType="" />
        auto new_entry = &create_node(*addresses, "t:Entry", mail.value());
        auto ptr_to_key = doc->allocate_string("Key");
        auto ptr_to_value = doc->allocate_string(key);
        new_entry->append_attribute(
            doc->allocate_attribute(ptr_to_key, ptr_to_value));
        if (!mail.name().empty())
        {
            ptr_to_key = doc->allocate_string("Name");
            ptr_to_value = doc->allocate_string(mail.name().c_str());
            new_entry->append_attribute(
                doc->allocate_attribute(ptr_to_key, ptr_to_value));
        }
        if (!mail.routing_type().empty())
        {
            ptr_to_key = doc->allocate_string("RoutingType");
            ptr_to_value = doc->allocate_string(mail.routing_type().c_str());
            new_entry->append_attribute(
                doc->allocate_attribute(ptr_to_key, ptr_to_value));
        }
        if (!mail.mailbox_type().empty())
        {
            ptr_to_key = doc->allocate_string("MailboxType");
            ptr_to_value = doc->allocate_string(mail.mailbox_type().c_str());
            new_entry->append_attribute(
                doc->allocate_attribute(ptr_to_key, ptr_to_value));
        }
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<contact>::value);
static_assert(std::is_copy_constructible<contact>::value);
static_assert(std::is_copy_assignable<contact>::value);
static_assert(std::is_move_constructible<contact>::value);
static_assert(std::is_move_assignable<contact>::value);
#endif

//! \brief Holds a subset of properties from an existing calendar item.
//!
//! \sa calendar_item::get_first_occurrence
//! \sa calendar_item::get_last_occurrence
//! \sa calendar_item::get_modified_occurrences
//! \sa calendar_item::get_deleted_occurrences
class occurrence_info final
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    occurrence_info() = default;
#else
    occurrence_info() {}
#endif

    occurrence_info(item_id id, date_time start, date_time end,
                    date_time original_start)
        : item_id_(std::move(id)), start_(std::move(start)),
          end_(std::move(end)), original_start_(std::move(original_start))
    {
    }

    //! True if this occurrence_info is undefined
    bool none() const EWS_NOEXCEPT { return !item_id_.valid(); }

    const item_id& get_item_id() const EWS_NOEXCEPT { return item_id_; }

    const date_time& get_start() const EWS_NOEXCEPT { return start_; }

    const date_time& get_end() const EWS_NOEXCEPT { return end_; }

    const date_time& get_original_start() const EWS_NOEXCEPT
    {
        return original_start_;
    }

    //! \brief Makes a occurrence_info instance from a \<FirstOccurrence>,
    //! \<LastOccurrence>, or \<Occurrence> XML element.
    static occurrence_info from_xml_element(const rapidxml::xml_node<>& elem)
    {
        using rapidxml::internal::compare;

        date_time original_start;
        date_time end;
        date_time start;
        item_id id;

        for (auto node = elem.first_node(); node; node = node->next_sibling())
        {
            if (compare(node->local_name(), node->local_name_size(),
                        "OriginalStart", strlen("OriginalStart")))
            {
                original_start =
                    date_time(std::string(node->value(), node->value_size()));
            }
            else if (compare(node->local_name(), node->local_name_size(), "End",
                             strlen("End")))
            {
                end = date_time(std::string(node->value(), node->value_size()));
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "Start", strlen("Start")))
            {
                start =
                    date_time(std::string(node->value(), node->value_size()));
            }
            else if (compare(node->local_name(), node->local_name_size(),
                             "ItemId", strlen("ItemId")))
            {
                id = item_id::from_xml_element(*node);
            }
            else
            {
                throw exception("Unexpected child element in <Mailbox>");
            }
        }

        return occurrence_info(std::move(id), std::move(start), std::move(end),
                               std::move(original_start));
    }

private:
    item_id item_id_;
    date_time start_;
    date_time end_;
    date_time original_start_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<occurrence_info>::value);
static_assert(std::is_copy_constructible<occurrence_info>::value);
static_assert(std::is_copy_assignable<occurrence_info>::value);
static_assert(std::is_move_constructible<occurrence_info>::value);
static_assert(std::is_move_assignable<occurrence_info>::value);
#endif

//! Abstract base class for all recurrence patterns.
class recurrence_pattern
{
public:
    virtual std::string get_occurence_name() { return "recurrence_pattern"; }
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    virtual ~recurrence_pattern() = default;

    recurrence_pattern(const recurrence_pattern&) = delete;
    recurrence_pattern& operator=(const recurrence_pattern&) = delete;
#else
    virtual ~recurrence_pattern() {}

private:
    recurrence_pattern(const recurrence_pattern&);
    recurrence_pattern& operator=(const recurrence_pattern&);

public:
#endif

    std::string to_xml() const { return this->to_xml_impl(); }

    //! \brief Creates a new XML element for this recurrence pattern and
    //! appends it to given parent node.
    //!
    //! Returns a reference to the newly created element.
    rapidxml::xml_node<>& to_xml_element(rapidxml::xml_node<>& parent) const
    {
        return this->to_xml_element_impl(parent);
    }

    //! \brief Makes a recurrence_pattern instance from a \<Recurrence> XML
    //! element.
    static std::unique_ptr<recurrence_pattern>
    from_xml_element(const rapidxml::xml_node<>& elem); // Defined below

protected:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    recurrence_pattern() = default;
#else
    recurrence_pattern() {}
#endif

private:
    virtual std::string to_xml_impl() const = 0;

    virtual rapidxml::xml_node<>&
    to_xml_element_impl(rapidxml::xml_node<>&) const = 0;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<recurrence_pattern>::value);
static_assert(!std::is_copy_constructible<recurrence_pattern>::value);
static_assert(!std::is_copy_assignable<recurrence_pattern>::value);
static_assert(!std::is_move_constructible<recurrence_pattern>::value);
static_assert(!std::is_move_assignable<recurrence_pattern>::value);
#endif

//! \brief An event that occurs annually relative to a month, week, and
//! day.
//!
//! Describes an annual relative recurrence, e.g., every third Monday in
//! April.
class relative_yearly_recurrence final : public recurrence_pattern
{
public:
    relative_yearly_recurrence(day_of_week days_of_week,
                               day_of_week_index index, month m)
        : days_of_week_(days_of_week), index_(index), month_(m)
    {
    }

    std::string get_occurence_name() override
    {
        return "relative_yearly_recurrence";
    }

    day_of_week get_days_of_week() const EWS_NOEXCEPT { return days_of_week_; }

    day_of_week_index get_day_of_week_index() const EWS_NOEXCEPT
    {
        return index_;
    }

    month get_month() const EWS_NOEXCEPT { return month_; }

private:
    day_of_week days_of_week_;
    day_of_week_index index_;
    month month_;

    std::string to_xml_impl() const override
    {
        using namespace internal;

        std::stringstream sstr;
        sstr << "<t:RelativeYearlyRecurrence>"
             << "<t:DaysOfWeek>" << enum_to_str(days_of_week_)
             << "</t:DaysOfWeek>"
             << "<t:DayOfWeekIndex>" << enum_to_str(index_)
             << "</t:DayOfWeekIndex>"
             << "<t:Month>" << enum_to_str(month_) << "</t:Month>"
             << "</t:RelativeYearlyRecurrence>";
        return sstr.str();
    }

    rapidxml::xml_node<>&
    to_xml_element_impl(rapidxml::xml_node<>& parent) const override
    {
        check(parent.document(), "Parent node needs to be part of a document");

        using namespace internal;
        auto& pattern_node = create_node(parent, "t:RelativeYearlyRecurrence");
        create_node(pattern_node, "t:DaysOfWeek", enum_to_str(days_of_week_));
        create_node(pattern_node, "t:DayOfWeekIndex", enum_to_str(index_));
        create_node(pattern_node, "t:Month", enum_to_str(month_));
        return pattern_node;
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(
    !std::is_default_constructible<relative_yearly_recurrence>::value);
static_assert(!std::is_copy_constructible<relative_yearly_recurrence>::value);
static_assert(!std::is_copy_assignable<relative_yearly_recurrence>::value);
static_assert(!std::is_move_constructible<relative_yearly_recurrence>::value);
static_assert(!std::is_move_assignable<relative_yearly_recurrence>::value);
#endif

//! A yearly recurrence pattern, e.g., a birthday.
class absolute_yearly_recurrence final : public recurrence_pattern
{
public:
    absolute_yearly_recurrence(uint32_t day_of_month, month m)
        : day_of_month_(day_of_month), month_(m)
    {
    }

    std::string get_occurence_name() override
    {
        return "absolute_yearly_recurrence";
    }

    uint32_t get_day_of_month() const EWS_NOEXCEPT { return day_of_month_; }

    month get_month() const EWS_NOEXCEPT { return month_; }

private:
    uint32_t day_of_month_;
    month month_;

    std::string to_xml_impl() const override
    {
        using namespace internal;

        std::stringstream sstr;
        sstr << "<t:AbsoluteYearlyRecurrence>"
             << "<t:DayOfMonth>" << day_of_month_ << "</t:DayOfMonth>"
             << "<t:Month>" << enum_to_str(month_) << "</t:Month>"
             << "</t:AbsoluteYearlyRecurrence>";
        return sstr.str();
    }

    rapidxml::xml_node<>&
    to_xml_element_impl(rapidxml::xml_node<>& parent) const override
    {
        using namespace internal;
        check(parent.document(), "Parent node needs to be part of a document");

        auto& pattern_node = create_node(parent, "t:AbsoluteYearlyRecurrence");
        create_node(pattern_node, "t:DayOfMonth",
                    std::to_string(day_of_month_));
        create_node(pattern_node, "t:Month", internal::enum_to_str(month_));
        return pattern_node;
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(
    !std::is_default_constructible<absolute_yearly_recurrence>::value);
static_assert(!std::is_copy_constructible<absolute_yearly_recurrence>::value);
static_assert(!std::is_copy_assignable<absolute_yearly_recurrence>::value);
static_assert(!std::is_move_constructible<absolute_yearly_recurrence>::value);
static_assert(!std::is_move_assignable<absolute_yearly_recurrence>::value);
#endif

//! \brief An event that occurs on the same day each month or monthly
//! interval.
//!
//! A good example is payment of a rent that is due on the second of each
//! month.
//!
//! \code
//! auto rent_is_due = ews::absolute_monthly_recurrence(1, 2);
//! \endcode
//!
//! The \p interval parameter indicates the interval in months between
//! each time zone. For example, an \p interval value of 1 would yield an
//! appointment occurring twelve times a year, a value of 6 would produce
//! two occurrences a year and so on.
class absolute_monthly_recurrence final : public recurrence_pattern
{
public:
    absolute_monthly_recurrence(uint32_t interval, uint32_t day_of_month)
        : interval_(interval), day_of_month_(day_of_month)
    {
    }

    std::string get_occurence_name() override
    {
        return "absolute_monthly_recurrence";
    }

    uint32_t get_interval() const EWS_NOEXCEPT { return interval_; }

    uint32_t get_days_of_month() const EWS_NOEXCEPT { return day_of_month_; }

private:
    uint32_t interval_;
    uint32_t day_of_month_;

    std::string to_xml_impl() const override
    {
        using namespace internal;

        std::stringstream sstr;
        sstr << "<t:AbsoluteMonthlyRecurrence>"
             << "<t:Interval>" << interval_ << "</t:Interval>"
             << "<t:DayOfMonth>" << day_of_month_ << "</t:DayOfMonth>"
             << "</t:AbsoluteMonthlyRecurrence>";
        return sstr.str();
    }

    rapidxml::xml_node<>&
    to_xml_element_impl(rapidxml::xml_node<>& parent) const override
    {
        using internal::create_node;
        check(parent.document(), "Parent node needs to be part of a document");

        auto& pattern_node = create_node(parent, "t:AbsoluteMonthlyRecurrence");
        create_node(pattern_node, "t:Interval", std::to_string(interval_));
        create_node(pattern_node, "t:DayOfMonth",
                    std::to_string(day_of_month_));
        return pattern_node;
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(
    !std::is_default_constructible<absolute_monthly_recurrence>::value);
static_assert(!std::is_copy_constructible<absolute_monthly_recurrence>::value);
static_assert(!std::is_copy_assignable<absolute_monthly_recurrence>::value);
static_assert(!std::is_move_constructible<absolute_monthly_recurrence>::value);
static_assert(!std::is_move_assignable<absolute_monthly_recurrence>::value);
#endif

//! \brief An event that occurs annually relative to a month, week, and
//! day.
//!
//! For example, if you are a member of a C++ user group that decides to
//! meet on the third Thursday of every month you would write
//!
//! \code
//! auto meetup =
//!        ews::relative_monthly_recurrence(1,
//!                                         ews::day_of_week::thu,
//!                                         ews::day_of_week_index::third);
//! \endcode
//!
//! The \p interval parameter indicates the interval in months between
//! each time zone. For example, an \p interval value of 1 would yield an
//! appointment occurring twelve times a year, a value of 6 would produce
//! two occurrences a year and so on.
class relative_monthly_recurrence final : public recurrence_pattern
{
public:
    relative_monthly_recurrence(uint32_t interval, day_of_week days_of_week,
                                day_of_week_index index)
        : interval_(interval), days_of_week_(days_of_week), index_(index)
    {
    }

    std::string get_occurence_name() override
    {
        return "relative_monthly_recurrence";
    }

    uint32_t get_interval() const EWS_NOEXCEPT { return interval_; }

    day_of_week get_days_of_week() const EWS_NOEXCEPT { return days_of_week_; }

    day_of_week_index get_day_of_week_index() const EWS_NOEXCEPT
    {
        return index_;
    }

private:
    uint32_t interval_;
    day_of_week days_of_week_;
    day_of_week_index index_;

    std::string to_xml_impl() const override
    {
        using namespace internal;

        std::stringstream sstr;
        sstr << "<t:RelativeMonthlyRecurrence>"
             << "<t:Interval>" << interval_ << "</t:Interval>"
             << "<t:DaysOfWeek>" << enum_to_str(days_of_week_)
             << "</t:DaysOfWeek>"
             << "<t:DayOfWeekIndex>" << enum_to_str(index_)
             << "</t:DayOfWeekIndex>"
             << "</t:RelativeMonthlyRecurrence>";
        return sstr.str();
    }

    rapidxml::xml_node<>&
    to_xml_element_impl(rapidxml::xml_node<>& parent) const override
    {
        using internal::create_node;
        auto& pattern_node = create_node(parent, "t:RelativeMonthlyRecurrence");
        create_node(pattern_node, "t:Interval", std::to_string(interval_));
        create_node(pattern_node, "t:DaysOfWeek",
                    internal::enum_to_str(days_of_week_));
        create_node(pattern_node, "t:DayOfWeekIndex",
                    internal::enum_to_str(index_));
        return pattern_node;
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(
    !std::is_default_constructible<relative_monthly_recurrence>::value);
static_assert(!std::is_copy_constructible<relative_monthly_recurrence>::value);
static_assert(!std::is_copy_assignable<relative_monthly_recurrence>::value);
static_assert(!std::is_move_constructible<relative_monthly_recurrence>::value);
static_assert(!std::is_move_assignable<relative_monthly_recurrence>::value);
#endif

//! \brief A weekly recurrence
//!
//! An example for a weekly recurrence is a regular meeting on a specific
//! day each week.
//!
//! \code
//! auto standup =
//!        ews::weekly_recurrence(1, ews::day_of_week::mon);
//! \endcode
class weekly_recurrence final : public recurrence_pattern
{
public:
    weekly_recurrence(uint32_t interval, day_of_week days_of_week,
                      day_of_week first_day_of_week = day_of_week::mon)
        : interval_(interval), days_of_week_(),
          first_day_of_week_(first_day_of_week)
    {
        days_of_week_.push_back(days_of_week);
    }

    weekly_recurrence(uint32_t interval, std::vector<day_of_week> days_of_week,
                      day_of_week first_day_of_week = day_of_week::mon)
        : interval_(interval), days_of_week_(std::move(days_of_week)),
          first_day_of_week_(first_day_of_week)
    {
    }

    std::string get_occurence_name() override { return "weekly_recurrence"; }

    uint32_t get_interval() const EWS_NOEXCEPT { return interval_; }

    const std::vector<day_of_week>& get_days_of_week() const EWS_NOEXCEPT
    {
        return days_of_week_;
    }

    day_of_week get_first_day_of_week() const EWS_NOEXCEPT
    {
        return first_day_of_week_;
    }

private:
    uint32_t interval_;
    std::vector<day_of_week> days_of_week_;
    day_of_week first_day_of_week_;

    std::string to_xml_impl() const override
    {
        using namespace internal;

        std::string value;
        for (const auto& day : days_of_week_)
        {
            value += enum_to_str(day) + " ";
        }
        value.resize(value.size() - 1);
        std::stringstream sstr;
        sstr << "<t:WeeklyRecurrence>"
             << "<t:Interval>" << interval_ << "</t:Interval>"
             << "<t:DaysOfWeek>" << value << "</t:DaysOfWeek>"
             << "<t:FirstDayOfWeek>" << enum_to_str(first_day_of_week_)
             << "</t:FirstDayOfWeek>"
             << "</t:WeeklyRecurrence>";
        return sstr.str();
    }

    rapidxml::xml_node<>&
    to_xml_element_impl(rapidxml::xml_node<>& parent) const override
    {
        using namespace internal;
        check(parent.document(), "Parent node needs to be part of a document");

        auto& pattern_node = create_node(parent, "t:WeeklyRecurrence");
        create_node(pattern_node, "t:Interval", std::to_string(interval_));
        std::string value;
        for (const auto& day : days_of_week_)
        {
            value += enum_to_str(day) + " ";
        }
        value.pop_back();
        create_node(pattern_node, "t:DaysOfWeek", value);
        create_node(pattern_node, "t:FirstDayOfWeek",
                    enum_to_str(first_day_of_week_));
        return pattern_node;
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<weekly_recurrence>::value);
static_assert(!std::is_copy_constructible<weekly_recurrence>::value);
static_assert(!std::is_copy_assignable<weekly_recurrence>::value);
static_assert(!std::is_move_constructible<weekly_recurrence>::value);
static_assert(!std::is_move_assignable<weekly_recurrence>::value);
#endif

//! \brief Describes a daily recurring event
class daily_recurrence final : public recurrence_pattern
{
public:
    explicit daily_recurrence(uint32_t interval) : interval_(interval) {}

    std::string get_occurence_name() override { return "daily_recurrence"; }

    uint32_t get_interval() const EWS_NOEXCEPT { return interval_; }

private:
    uint32_t interval_;

    std::string to_xml_impl() const override
    {
        using namespace internal;

        std::stringstream sstr;
        sstr << "<t:DailyRecurrence>"
             << "<t:Interval>" << interval_ << "</t:Interval>"
             << "</t:DailyRecurrence>";
        return sstr.str();
    }

    rapidxml::xml_node<>&
    to_xml_element_impl(rapidxml::xml_node<>& parent) const override
    {
        using internal::create_node;
        check(parent.document(), "Parent node needs to be part of a document");

        auto& pattern_node = create_node(parent, "t:DailyRecurrence");
        create_node(pattern_node, "t:Interval", std::to_string(interval_));
        return pattern_node;
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<daily_recurrence>::value);
static_assert(!std::is_copy_constructible<daily_recurrence>::value);
static_assert(!std::is_copy_assignable<daily_recurrence>::value);
static_assert(!std::is_move_constructible<daily_recurrence>::value);
static_assert(!std::is_move_assignable<daily_recurrence>::value);
#endif

//! Abstract base class for all recurrence ranges.
class recurrence_range
{
public:
    virtual std::string get_reccurence_range_name()
    {
        return "recurrence_range";
    }
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    virtual ~recurrence_range() = default;

    recurrence_range(const recurrence_range&) = delete;
    recurrence_range& operator=(const recurrence_range&) = delete;
#else
    virtual ~recurrence_range() {}

private:
    recurrence_range(const recurrence_range&);
    recurrence_range& operator=(const recurrence_range&);

public:
#endif
    std::string to_xml() const { return this->to_xml_impl(); }

    //! \brief Creates a new XML element for this recurrence range and
    //! appends it to given parent node.
    //!
    //! Returns a reference to the newly created element.
    rapidxml::xml_node<>& to_xml_element(rapidxml::xml_node<>& parent) const
    {
        return this->to_xml_element_impl(parent);
    }

    //! Makes a recurrence_range instance from a \<Recurrence> XML element.
    static std::unique_ptr<recurrence_range>
    from_xml_element(const rapidxml::xml_node<>& elem); // Defined below

protected:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    recurrence_range() = default;
#else
    recurrence_range() {}
#endif

private:
    virtual std::string to_xml_impl() const = 0;

    virtual rapidxml::xml_node<>&
    to_xml_element_impl(rapidxml::xml_node<>&) const = 0;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<recurrence_range>::value);
static_assert(!std::is_copy_constructible<recurrence_range>::value);
static_assert(!std::is_copy_assignable<recurrence_range>::value);
static_assert(!std::is_move_constructible<recurrence_range>::value);
static_assert(!std::is_move_assignable<recurrence_range>::value);
#endif

//! Represents recurrence range with no end date
class no_end_recurrence_range final : public recurrence_range
{
public:
    explicit no_end_recurrence_range(date start_date)
        : start_date_(std::move(start_date))
    {
    }

    std::string get_reccurence_range_name() override
    {
        return "no_end_recurrence_range";
    }

    const date_time& get_start_date() const EWS_NOEXCEPT { return start_date_; }

private:
    date start_date_;

    std::string to_xml_impl() const override
    {
        using namespace internal;

        std::stringstream sstr;
        sstr << "<t:NoEndRecurrence>"
             << "<t:StartDate>" << start_date_.to_string() << "</t:StartDate>"
             << "</t:NoEndRecurrence>";
        return sstr.str();
    }

    rapidxml::xml_node<>&
    to_xml_element_impl(rapidxml::xml_node<>& parent) const override
    {
        using namespace internal;
        check(parent.document(), "Parent node needs to be part of a document");

        auto& pattern_node = create_node(parent, "t:NoEndRecurrence");
        create_node(pattern_node, "t:StartDate", start_date_.to_string());
        return pattern_node;
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<no_end_recurrence_range>::value);
static_assert(!std::is_copy_constructible<no_end_recurrence_range>::value);
static_assert(!std::is_copy_assignable<no_end_recurrence_range>::value);
static_assert(!std::is_move_constructible<no_end_recurrence_range>::value);
static_assert(!std::is_move_assignable<no_end_recurrence_range>::value);
#endif

//! Represents recurrence range with end date
class end_date_recurrence_range final : public recurrence_range
{
public:
    end_date_recurrence_range(date start_date, date end_date)
        : start_date_(std::move(start_date)), end_date_(std::move(end_date))
    {
    }

    std::string get_reccurence_range_name() override
    {
        return "end_date_recurrence_range";
    }

    const date_time& get_start_date() const EWS_NOEXCEPT { return start_date_; }

    const date_time& get_end_date() const EWS_NOEXCEPT { return end_date_; }

private:
    date start_date_;
    date end_date_;

    std::string to_xml_impl() const override
    {
        using namespace internal;

        std::stringstream sstr;
        sstr << "<t:EndDateRecurrence>"
             << "<t:StartDate>" << start_date_.to_string() << "</t:StartDate>"
             << "<t:EndDate>" << end_date_.to_string() << "</t:EndDate>"
             << "</t:EndDateRecurrence>";
        return sstr.str();
    }

    rapidxml::xml_node<>&
    to_xml_element_impl(rapidxml::xml_node<>& parent) const override
    {
        using internal::create_node;
        check(parent.document(), "Parent node needs to be part of a document");

        auto& pattern_node = create_node(parent, "t:EndDateRecurrence");
        create_node(pattern_node, "t:StartDate", start_date_.to_string());
        create_node(pattern_node, "t:EndDate", end_date_.to_string());
        return pattern_node;
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<end_date_recurrence_range>::value);
static_assert(!std::is_copy_constructible<end_date_recurrence_range>::value);
static_assert(!std::is_copy_assignable<end_date_recurrence_range>::value);
static_assert(!std::is_move_constructible<end_date_recurrence_range>::value);
static_assert(!std::is_move_assignable<end_date_recurrence_range>::value);
#endif

//! Represents a numbered recurrence range
class numbered_recurrence_range final : public recurrence_range
{
public:
    numbered_recurrence_range(date start_date, uint32_t no_of_occurrences)
        : start_date_(std::move(start_date)),
          no_of_occurrences_(no_of_occurrences)
    {
    }

    std::string get_reccurence_range_name() override
    {
        return "numbered_recurrence_range";
    }

    const date_time& get_start_date() const EWS_NOEXCEPT { return start_date_; }

    uint32_t get_number_of_occurrences() const EWS_NOEXCEPT
    {
        return no_of_occurrences_;
    }

private:
    date start_date_;
    uint32_t no_of_occurrences_;

    std::string to_xml_impl() const override
    {
        using namespace internal;

        std::stringstream sstr;
        sstr << "<t:NumberedRecurrence>"
             << "<t:StartDate>" << start_date_.to_string() << "</t:StartDate>"
             << "<t:NumberOfOccurrences>" << no_of_occurrences_
             << "</t:NumberOfOccurrences>"
             << "</t:NumberedRecurrence>";
        return sstr.str();
    }

    rapidxml::xml_node<>&
    to_xml_element_impl(rapidxml::xml_node<>& parent) const override
    {
        using namespace internal;
        check(parent.document(), "Parent node needs to be part of a document");

        auto& pattern_node = create_node(parent, "t:NumberedRecurrence");
        create_node(pattern_node, "t:StartDate", start_date_.to_string());
        create_node(pattern_node, "t:NumberOfOccurrences",
                    std::to_string(no_of_occurrences_));
        return pattern_node;
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<numbered_recurrence_range>::value);
static_assert(!std::is_copy_constructible<numbered_recurrence_range>::value);
static_assert(!std::is_copy_assignable<numbered_recurrence_range>::value);
static_assert(!std::is_move_constructible<numbered_recurrence_range>::value);
static_assert(!std::is_move_assignable<numbered_recurrence_range>::value);
#endif

//! Represents a calendar item in the Exchange store
class calendar_item final : public item
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    calendar_item() = default;
#else
    calendar_item() {}
#endif

    //! Creates a <tt>\<CalendarItem\></tt> with given id
    explicit calendar_item(item_id id) : item(id) {}

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
    calendar_item(item_id&& id, internal::xml_subtree&& properties)
        : item(std::move(id), std::move(properties))
    {
    }
#endif

    //! Returns the starting date and time for this calendar item
    date_time get_start() const
    {
        return date_time(xml().get_value_as_string("Start"));
    }

    //! Sets the starting date and time for this calendar item
    void set_start(const date_time& datetime)
    {
        xml().set_or_update("Start", datetime.to_string());
    }

    //! Returns the ending date and time for this calendar item
    date_time get_end() const
    {
        return date_time(xml().get_value_as_string("End"));
    }

    //! Sets the ending date and time for this calendar item
    void set_end(const date_time& datetime)
    {
        xml().set_or_update("End", datetime.to_string());
    }

    //! \brief The original start time of a calendar item
    //!
    //! This is a read-only property.
    date_time get_original_start() const
    {
        return date_time(xml().get_value_as_string("OriginalStart"));
    }

    //! True if this calendar item is lasting all day
    bool is_all_day_event() const
    {
        return xml().get_value_as_string("IsAllDayEvent") == "true";
    }

    //! Makes this calendar item an all day event or not
    void set_all_day_event_enabled(bool enabled)
    {
        xml().set_or_update("IsAllDayEvent", enabled ? "true" : "false");
    }

    //! Returns the free/busy status of this calendar item
    free_busy_status get_legacy_free_busy_status() const
    {
        const auto val = xml().get_value_as_string("LegacyFreeBusyStatus");
        // Default seems to be 'Busy' if not explicitly set
        if (val.empty() || val == "Busy")
        {
            return free_busy_status::busy;
        }
        if (val == "Tentative")
        {
            return free_busy_status::tentative;
        }
        if (val == "Free")
        {
            return free_busy_status::free;
        }
        if (val == "OOF")
        {
            return free_busy_status::out_of_office;
        }
        if (val == "NoData")
        {
            return free_busy_status::no_data;
        }
        if (val == "WorkingElsewhere")
        {
            return free_busy_status::working_elsewhere;
        }
        throw exception("Unexpected <LegacyFreeBusyStatus>");
    }

    //! Sets the free/busy status of this calendar item
    void set_legacy_free_busy_status(free_busy_status status)
    {
        xml().set_or_update("LegacyFreeBusyStatus",
                            internal::enum_to_str(status));
    }

    //! \brief Returns the location where a meeting or event is supposed
    //! to take place.
    std::string get_location() const
    {
        return xml().get_value_as_string("Location");
    }

    //! \brief Sets the location where a meeting or event is supposed to
    //! take place.
    void set_location(const std::string& location)
    {
        xml().set_or_update("Location", location);
    }

    //! Returns a description of when this calendar item occurs
    std::string get_when() const { return xml().get_value_as_string("When"); }

    //! Sets a description of when this calendar item occurs
    void set_when(const std::string& desc)
    {
        xml().set_or_update("When", desc);
    }

    //! \brief Indicates whether this calendar item is a meeting
    //!
    //! This is a read-only property.
    bool is_meeting() const
    {
        return xml().get_value_as_string("IsMeeting") == "true";
    }

    //! \brief Indicates whether this calendar item has been cancelled by
    //! the organizer.
    //!
    //! This is a read-only property. It is only meaningful for meetings.
    bool is_cancelled() const
    {
        return xml().get_value_as_string("IsCancelled") == "true";
    }

    //! \brief True if a calendar item is part of a recurring series.
    //!
    //! This is a read-only property. Note that a recurring master is not
    //! considered part of a recurring series, even though it holds the
    //! recurrence information.
    bool is_recurring() const
    {
        return xml().get_value_as_string("IsRecurring") == "true";
    }

    //! \brief True if a meeting request for this calendar item has been
    //! sent to all attendees.
    //!
    //! This is a read-only property.
    bool meeting_request_was_sent() const
    {
        return xml().get_value_as_string("MeetingRequestWasSent") == "true";
    }

    //! \brief Indicates whether a response to a calendar item is needed.
    //!
    //! This is a read-only property.
    bool is_response_requested() const
    {
        return xml().get_value_as_string("IsResponseRequested") == "true";
    }

    //! \brief Returns the type of this calendar item
    //!
    //! This is a read-only property.
    calendar_item_type get_calendar_item_type() const
    {
        const auto val = xml().get_value_as_string("CalendarItemType");
        // By default, newly created calendar items are of type 'Single'
        if (val.empty() || val == "Single")
        {
            return calendar_item_type::single;
        }
        if (val == "Occurrence")
        {
            return calendar_item_type::occurrence;
        }
        if (val == "Exception")
        {
            return calendar_item_type::exception;
        }
        if (val == "RecurringMaster")
        {
            return calendar_item_type::recurring_master;
        }
        throw exception("Unexpected <CalendarItemType>");
    }

    //! \brief Returns the response of this calendar item's owner to the
    //! meeting
    //!
    //! This is a read-only property.
    response_type get_my_response_type() const
    {
        const auto val = xml().get_value_as_string("MyResponseType");
        if (val.empty())
        {
            return response_type::unknown;
        }
        return internal::str_to_response_type(val);
    }

    //! \brief Returns the organizer of this calendar item
    //!
    //! For meetings, the party responsible for coordinating attendance.
    //! This is a read-only property.
    mailbox get_organizer() const
    {
        const auto organizer = xml().get_node("Organizer");
        if (!organizer)
        {
            return mailbox(); // None
        }
        return mailbox::from_xml_element(*(organizer->first_node()));
    }

    //! Returns all attendees required to attend this meeting
    std::vector<attendee> get_required_attendees() const
    {
        return get_attendees_helper("RequiredAttendees");
    }

    //! Sets the attendees required to attend this meeting
    void set_required_attendees(const std::vector<attendee>& attendees) const
    {
        set_attendees_helper("RequiredAttendees", attendees);
    }

    //! Returns all attendees not required to attend this meeting
    std::vector<attendee> get_optional_attendees() const
    {
        return get_attendees_helper("OptionalAttendees");
    }

    //! Sets the attendees not required to attend this meeting
    void set_optional_attendees(const std::vector<attendee>& attendees) const
    {
        set_attendees_helper("OptionalAttendees", attendees);
    }

    //! Returns all scheduled resources of this meeting
    std::vector<attendee> get_resources() const
    {
        return get_attendees_helper("Resources");
    }

    //! Sets the scheduled resources of this meeting
    void set_resources(const std::vector<attendee>& resources) const
    {
        set_attendees_helper("Resources", resources);
    }

    //! \brief Returns the number of meetings that are in conflict with
    //! this meeting
    //!
    //! Note: this property is only included in \<GetItem/> response when
    //! 'calendar:ConflictingMeetingCount' is passed in
    //! \<AdditionalProperties/>. This is a read-only property.
    //! \sa service::get_conflicting_meetings
    int get_conflicting_meeting_count() const
    {
        const auto val = xml().get_value_as_string("ConflictingMeetingCount");
        return val.empty() ? 0 : std::stoi(val);
    }

    //! \brief Returns the number of meetings that are adjacent to this
    //! meeting
    //!
    //! Note: this property is only included in \<GetItem/> response when
    //! 'calendar:AdjacentMeetingCount' is passed in
    //! \<AdditionalProperties/>. This is a read-only property.
    //! \sa service::get_adjacent_meetings
    int get_adjacent_meeting_count() const
    {
        const auto val = xml().get_value_as_string("AdjacentMeetingCount");
        return val.empty() ? 0 : std::stoi(val);
    }

    // TODO: issue #19
    // <ConflictingMeetings/>
    // <AdjacentMeetings/>

    //! \brief Returns the duration of this meeting.
    //!
    //! This is a read-only property.
    duration get_duration() const
    {
        return duration(xml().get_value_as_string("Duration"));
    }

    //! \brief Returns the display name for the time zone associated with
    //! this calendar item.
    //!
    //! Provides a text-only description of the time zone for a calendar
    //! item.
    //!
    //! Note: This is a read-only property. EWS does not allow you to
    //! specify the name of a time zone for a calendar item with
    //! \<CreateItem/> and \<UpdateItem/>. Hence, any calendar items that
    //! is fetched with EWS that were also created with EWS will always
    //! have an empty \<TimeZone/> property.
    //!
    //! \sa get_meeting_time_zone, set_meeting_time_zone
    std::string get_time_zone() const
    {
        return xml().get_value_as_string("TimeZone");
    }

    //! \brief Returns the date and time when when this meeting was
    //! responded to.
    //!
    //! Note: Applicable to meetings only. This is a read-only property
    date_time get_appointment_reply_time() const
    {
        return date_time(xml().get_value_as_string("AppointmentReplyTime"));
    }

    //! \brief Returns the sequence number of this meetings version
    //!
    //! Note: Applicable to meetings only. This is a read-only property
    int get_appointment_sequence_number() const
    {
        const auto val = xml().get_value_as_string("AppointmentSequenceNumber");
        return val.empty() ? 0 : std::stoi(val);
    }

    //! \brief Returns the status of this meeting.
    //!
    //! The returned integer is a bitmask.
    //!
    //! \li <tt>0x0000</tt> - No flags have been set. This is only used
    //!                       for a calendar item that does not include
    //!                       attendees
    //! \li <tt>0x0001</tt> - Appointment is a meeting
    //! \li <tt>0x0002</tt> - Appointment has been received
    //! \li <tt>0x0004</tt> - Appointment has been canceled
    //!
    //! Note: Applicable to meetings only and is only included in a
    //! meeting response. This is a read-only property
    int get_appointment_state() const
    {
        const auto val = xml().get_value_as_string("AppointmentState");
        return val.empty() ? 0 : std::stoi(val);
    }

    //! \brief Returns the recurrence pattern for calendar items and
    //! meeting requests.
    //!
    //! The returned pointers are null if this calendar item is not a
    //! recurring master.
    std::pair<std::unique_ptr<recurrence_pattern>,
              std::unique_ptr<recurrence_range>>
    get_recurrence() const
    {
        typedef std::pair<std::unique_ptr<recurrence_pattern>,
                          std::unique_ptr<recurrence_range>>
            return_type;
        auto node = xml().get_node("Recurrence");
        if (!node)
        {
            return return_type();
        }
        return std::make_pair(recurrence_pattern::from_xml_element(*node),
                              recurrence_range::from_xml_element(*node));
    }

    //! Sets the recurrence pattern for calendar items and meeting requests
    void set_recurrence(const recurrence_pattern& pattern,
                        const recurrence_range& range)
    {
        auto doc = xml().document();
        auto recurrence_node = xml().get_node("Recurrence");
        if (recurrence_node)
        {
            // Remove existing node first
            doc->remove_node(recurrence_node);
        }

        recurrence_node = &internal::create_node(*doc, "t:Recurrence");

        pattern.to_xml_element(*recurrence_node);
        range.to_xml_element(*recurrence_node);
    }

    //! Returns the first occurrence
    occurrence_info get_first_occurrence() const
    {
        auto node = xml().get_node("FirstOccurrence");
        if (!node)
        {
            return occurrence_info();
        }
        return occurrence_info::from_xml_element(*node);
    }

    //! Returns the last occurrence
    occurrence_info get_last_occurrence() const
    {
        auto node = xml().get_node("LastOccurrence");
        if (!node)
        {
            return occurrence_info();
        }
        return occurrence_info::from_xml_element(*node);
    }

    //! Returns the modified occurrences
    std::vector<occurrence_info> get_modified_occurrences() const
    {
        auto node = xml().get_node("ModifiedOccurrences");
        if (!node)
        {
            return std::vector<occurrence_info>();
        }

        auto occurrences = std::vector<occurrence_info>();
        for (auto occurrence = node->first_node(); occurrence;
             occurrence = occurrence->next_sibling())
        {
            occurrences.emplace_back(
                occurrence_info::from_xml_element(*occurrence));
        }
        return occurrences;
    }

    //! Returns the deleted occurrences
    std::vector<occurrence_info> get_deleted_occurrences() const
    {
        auto node = xml().get_node("DeletedOccurrences");
        if (!node)
        {
            return std::vector<occurrence_info>();
        }

        auto occurrences = std::vector<occurrence_info>();
        for (auto occurrence = node->first_node(); occurrence;
             occurrence = occurrence->next_sibling())
        {
            occurrences.emplace_back(
                occurrence_info::from_xml_element(*occurrence));
        }
        return occurrences;
    }

    //! Sets the time zone for the starting date and time
    void set_start_time_zone(time_zone tz)
    {
        internal::xml_subtree::attribute id_attribute = {
            "Id", internal::enum_to_str(tz)};
        std::vector<internal::xml_subtree::attribute> attributes;
        attributes.emplace_back(id_attribute);
        xml().set_or_update("StartTimeZone", attributes);
    }

    //! Returns the time zone for the starting date and time
    time_zone get_start_time_zone()
    {
        const auto val = xml().get_value_as_string("StartTimeZoneId");
        if (!val.empty())
            return internal::str_to_time_zone(val);

        const auto node = xml().get_node("StartTimeZone");
        if (!node)
            return time_zone::none;
        const auto att = node->first_attribute("Id");
        if (!att)
            return time_zone::none;
        return internal::str_to_time_zone(att->value());
    }

    //! Sets the time zone of the ending date and time
    void set_end_time_zone(time_zone tz)
    {
        internal::xml_subtree::attribute id_attribute = {
            "Id", internal::enum_to_str(tz)};
        std::vector<internal::xml_subtree::attribute> attributes;
        attributes.emplace_back(id_attribute);
        xml().set_or_update("EndTimeZone", attributes);
    }

    //! Returns the time zone for the ending date and time
    time_zone get_end_time_zone()
    {
        const auto val = xml().get_value_as_string("EndTimeZoneId");
        if (!val.empty())
            return internal::str_to_time_zone(val);

        const auto node = xml().get_node("EndTimeZone");
        if (!node)
            return time_zone::none;
        const auto att = node->first_attribute("Id");
        if (!att)
            return time_zone::none;
        return internal::str_to_time_zone(att->value());
    }

    //! Sets the time zone for the meeting date and time
    void set_meeting_time_zone(time_zone tz)
    {
        internal::xml_subtree::attribute id_attribute = {
            "Id", internal::enum_to_str(tz)};
        std::vector<internal::xml_subtree::attribute> attributes;
        attributes.emplace_back(id_attribute);
        xml().set_or_update("MeetingTimeZone", attributes);
    }

    //! Returns the time zone for the meeting date and time
    time_zone get_meeting_time_zone()
    {
        const auto val = xml().get_value_as_string("MeetingTimeZoneId");
        if (!val.empty())
            return internal::str_to_time_zone(val);

        const auto node = xml().get_node("MeetingTimeZone");
        if (!node)
            return time_zone::none;
        const auto att = node->first_attribute("Id");
        if (!att)
            return time_zone::none;
        return internal::str_to_time_zone(att->value());
    }

    //! \brief Returns the type of conferencing that is performed with this
    //! calendar item.
    //!
    //! Possible values:
    //!
    //! \li 0 - NetMeeting
    //! \li 1 - NetShow
    //! \li 2 - Chat
    int get_conference_type() const
    {
        const auto val = xml().get_value_as_string("ConferenceType");
        return val.empty() ? 0 : std::stoi(val);
    }

    //! \brief Sets the type of conferencing that is performed with this
    //! calendar item.
    //!
    //! \sa get_conference_type
    void set_conference_type(int value)
    {
        xml().set_or_update("ConferenceType", std::to_string(value));
    }

    //! \brief Returns true if attendees are allowed to respond to the
    //! organizer with new time suggestions.
    bool is_new_time_proposal_allowed() const
    {
        return xml().get_value_as_string("AllowNewTimeProposal") == "true";
    }

    //! \brief If set to true, allows attendees to respond to the organizer
    //! with new time suggestions.
    //!
    //! Note: This property is read-writable for the organizer's calendar
    //! item. For meeting requests and for attendees' calendar items this
    //! is read-only .
    void set_new_time_proposal_allowed(bool allowed)
    {
        xml().set_or_update("AllowNewTimeProposal", allowed ? "true" : "false");
    }

    //! \brief Returns whether this meeting is held online.
    bool is_online_meeting() const
    {
        return xml().get_value_as_string("IsOnlineMeeting") == "true";
    }

    //! \brief If set to true, this meeting is supposed to be held online.
    //!
    //! Note: This property is read-writable for the organizer's calendar
    //! item. For meeting requests and for attendees' calendar items this
    //! is read-only .
    void set_online_meeting_enabled(bool enabled)
    {
        xml().set_or_update("IsOnlineMeeting", enabled ? "true" : "false");
    }

    //! Returns the URL for a meeting workspace
    std::string get_meeting_workspace_url() const
    {
        return xml().get_value_as_string("MeetingWorkspaceUrl");
    }

    //! \brief Sets the URL for a meeting workspace.
    //!
    //! Note: This property is read-writable for the organizer's calendar
    //! item. For meeting requests and for attendees' calendar items this
    //! is read-only .
    void set_meeting_workspace_url(const std::string& url)
    {
        xml().set_or_update("MeetingWorkspaceUrl", url);
    }

    //! Returns a URL for Microsoft NetShow online meeting
    std::string get_net_show_url() const
    {
        return xml().get_value_as_string("NetShowUrl");
    }

    //! \brief Sets the URL for Microsoft NetShow online meeting.
    //!
    //! Note: This property is read-writable for the organizer's calendar
    //! item. For meeting requests and for attendees' calendar items this
    //! is read-only .
    void set_net_show_url(const std::string& url)
    {
        xml().set_or_update("NetShowUrl", url);
    }

    //! Makes a calendar item instance from a \<CalendarItem> XML element
    static calendar_item from_xml_element(const rapidxml::xml_node<>& elem)
    {
        auto id_node =
            elem.first_node_ns(internal::uri<>::microsoft::types(), "ItemId");
        check(id_node, "Expected <ItemId>");
        return calendar_item(item_id::from_xml_element(*id_node),
                             internal::xml_subtree(elem));
    }

private:
    inline std::vector<attendee>
    get_attendees_helper(const char* node_name) const
    {
        const auto attendees = xml().get_node(node_name);
        if (!attendees)
        {
            return std::vector<attendee>();
        }

        std::vector<attendee> result;
        for (auto attendee_node = attendees->first_node(); attendee_node;
             attendee_node = attendee_node->next_sibling())
        {
            result.emplace_back(attendee::from_xml_element(*attendee_node));
        }
        return result;
    }

    inline void
    set_attendees_helper(const char* node_name,
                         const std::vector<attendee>& attendees) const
    {
        auto doc = xml().document();

        auto attendees_node = xml().get_node(node_name);
        if (attendees_node)
        {
            doc->remove_node(attendees_node);
        }

        attendees_node =
            &internal::create_node(*doc, "t:" + std::string(node_name));

        for (const auto& a : attendees)
        {
            a.to_xml_element(*attendees_node);
        }
    }

    template <typename U> friend class basic_service;

    const std::string& item_tag_name() const EWS_NOEXCEPT
    {
        static const std::string name("CalendarItem");
        return name;
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<calendar_item>::value);
static_assert(std::is_copy_constructible<calendar_item>::value);
static_assert(std::is_copy_assignable<calendar_item>::value);
static_assert(std::is_move_constructible<calendar_item>::value);
static_assert(std::is_move_assignable<calendar_item>::value);
#endif

//! A message item in the Exchange store
class message final : public item
{
public:
//! Constructs a new message object
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    message() = default;
#else
    message() {}
#endif

    //! Constructs a new message object with the given id
    explicit message(item_id id) : item(id) {}

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
    message(item_id&& id, internal::xml_subtree&& properties)
        : item(std::move(id), std::move(properties))
    {
    }
#endif

    //! Returns the Sender: header field of this message.
    mailbox get_sender() const
    {
        const auto sender_node = xml().get_node("Sender");
        if (!sender_node)
        {
            return mailbox(); // None
        }
        return mailbox::from_xml_element(*(sender_node->first_node()));
    }

    //! Sets the Sender: header field of this message.
    void set_sender(const ews::mailbox& m)
    {
        auto doc = xml().document();
        auto sender_node = xml().get_node("Sender");
        if (sender_node)
        {
            doc->remove_node(sender_node);
        }
        sender_node = &internal::create_node(*doc, "t:Sender");
        m.to_xml_element(*sender_node);
    }

    //! Returns the recipients of this message.
    std::vector<mailbox> get_to_recipients() const
    {
        return get_recipients_impl("ToRecipients");
    }

    //! \brief Sets the recipients of this message to \p recipients.
    //!
    //! Setting this property sets the To: header field as described in RFC
    //! 5322.
    void set_to_recipients(const std::vector<mailbox>& recipients)
    {
        set_recipients_impl("ToRecipients", recipients);
    }

    //! Returns the Cc: recipients of this message.
    std::vector<mailbox> get_cc_recipients() const
    {
        return get_recipients_impl("CcRecipients");
    }

    //! \brief Sets the recipients that will receive a carbon copy of the
    //! message to \p recipients.
    //!
    //! Setting this property sets the Cc: header field as described in RFC
    //! 5322.
    void set_cc_recipients(const std::vector<mailbox>& recipients)
    {
        set_recipients_impl("CcRecipients", recipients);
    }

    //! Returns the Bcc: recipients of this message.
    std::vector<mailbox> get_bcc_recipients() const
    {
        return get_recipients_impl("BccRecipients");
    }

    //! \brief Sets the recipients that will receive a blind carbon copy of the
    //! message to \p recipients.
    //!
    //! Setting this property sets the Bcc: header field as described in RFC
    //! 5322.
    void set_bcc_recipients(const std::vector<mailbox>& recipients)
    {
        set_recipients_impl("BccRecipients", recipients);
    }

    // <IsReadReceiptRequested/>
    // <IsDeliveryReceiptRequested/>
    // <ConversationIndex/>
    // <ConversationTopic/>

    //! Returns the From: header field of this message.
    mailbox get_from() const
    {
        const auto from_node = xml().get_node("From");
        if (!from_node)
        {
            return mailbox(); // None set
        }
        return mailbox::from_xml_element(*(from_node->first_node()));
    }

    //! Sets the From: header field of this message.
    void set_from(const ews::mailbox& m)
    {
        auto doc = xml().document();
        auto from_node = xml().get_node("From");
        if (from_node)
        {
            doc->remove_node(from_node);
        }
        from_node = &internal::create_node(*doc, "t:From");
        m.to_xml_element(*from_node);
    }

    //! \brief Returns the Message-ID: header field of this email message.
    //!
    //! This function can be used to retrieve the \<InternetMessageId>
    //! property of this message. The property provides the unique message
    //! identifier according to the RFCs for email, RFC 822 and RFC 2822.
    std::string get_internet_message_id() const
    {
        return xml().get_value_as_string("InternetMessageId");
    }

    //! \brief Sets the Message-ID: header field of this email message.
    //!
    //! Note that it is not possible to change a message's Message-ID value.
    //! This means that updating this property via the
    //! <tt>ews::message_property_path::internet_message_id</tt> property path
    //! will most certainly be rejected by the Exchange server. However,
    //! setting this property when creating a new message is absolutely fine.
    //!
    //! \sa get_internet_message_id()
    void set_internet_message_id(const std::string& value)
    {
        xml().set_or_update("InternetMessageId", value);
    }

    //! Returns whether this message has been read
    bool is_read() const
    {
        return xml().get_value_as_string("IsRead") == "true";
    }

    //! \brief Sets whether this message has been read.
    //!
    //! If is_read_receipt_requested() evaluates to true, updating this
    //! property to true sends a read receipt.
    void set_is_read(bool value)
    {
        xml().set_or_update("IsRead", value ? "true" : "false");
    }

    // <IsResponseRequested/>
    // <References/>

    //! Returns the Reply-To: address list of this message.
    std::vector<mailbox> get_reply_to() const
    {
        return get_recipients_impl("ReplyTo");
    }

    //! \brief Sets the addresses to which replies to this message should be
    //! sent.
    //!
    //! Setting this property sets the Reply-To: header field as described in
    //! RFC 5322.
    void set_reply_to(const std::vector<mailbox>& recipients)
    {
        set_recipients_impl("ReplyTo", recipients);
    }

    //! Makes a message instance from a \<Message> XML element
    static message from_xml_element(const rapidxml::xml_node<>& elem)
    {
        auto id_node =
            elem.first_node_ns(internal::uri<>::microsoft::types(), "ItemId");
        check(id_node, "Expected <ItemId>");
        return message(item_id::from_xml_element(*id_node),
                       internal::xml_subtree(elem));
    }

private:
    template <typename U> friend class basic_service;

    const std::string& item_tag_name() const EWS_NOEXCEPT
    {
        static const std::string name("Message");
        return name;
    }

    std::vector<mailbox> get_recipients_impl(const char* node_name) const
    {
        const auto recipients = xml().get_node(node_name);
        if (!recipients)
        {
            return std::vector<mailbox>();
        }
        std::vector<mailbox> result;
        for (auto mailbox_node = recipients->first_node(); mailbox_node;
             mailbox_node = mailbox_node->next_sibling())
        {
            result.emplace_back(mailbox::from_xml_element(*mailbox_node));
        }
        return result;
    }

    void set_recipients_impl(const char* node_name,
                             const std::vector<mailbox>& recipients)
    {
        auto doc = xml().document();

        auto recipients_node = xml().get_node(node_name);
        if (recipients_node)
        {
            doc->remove_node(recipients_node);
        }

        recipients_node =
            &internal::create_node(*doc, std::string("t:") + node_name);

        for (const auto& recipient : recipients)
        {
            recipient.to_xml_element(*recipients_node);
        }
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<message>::value);
static_assert(std::is_copy_constructible<message>::value);
static_assert(std::is_copy_assignable<message>::value);
static_assert(std::is_move_constructible<message>::value);
static_assert(std::is_move_assignable<message>::value);
#endif

//! Identifies frequently referenced properties by an URI
class property_path
{
public:
    // Intentionally not explicit
    property_path(const char* uri) : uri_(uri) { class_name(); }

#ifdef EWS_HAS_DEFAULT_AND_DELETE
    virtual ~property_path() = default;
    property_path(const property_path&) = default;
    property_path& operator=(const property_path&) = default;
#else
    virtual ~property_path() {}
#endif

    std::string to_xml() const { return this->to_xml_impl(); }

    std::string to_xml(const std::string& value) const
    {
        return this->to_xml_impl(value);
    }

    //! Returns the value of the \<FieldURI> element
    const std::string& field_uri() const EWS_NOEXCEPT { return uri_; }

protected:
    std::string class_name() const
    {
        // TODO: we know at compile-time to which class a property belongs
        const auto n = uri_.find(':');
        check((n != std::string::npos), "Expected a ':' in URI");
        const auto substr = uri_.substr(0, n);
        if (substr == "folder")
        {
            return "Folder";
        }
        else if (substr == "item")
        {
            return "Item";
        }
        else if (substr == "message")
        {
            return "Message";
        }
        else if (substr == "meeting")
        {
            return "Meeting";
        }
        else if (substr == "meetingRequest")
        {
            return "MeetingRequest";
        }
        else if (substr == "calendar")
        {
            return "CalendarItem";
        }
        else if (substr == "task")
        {
            return "Task";
        }
        else if (substr == "contacts")
        {
            return "Contact";
        }
        else if (substr == "distributionlist")
        {
            return "DistributionList";
        }
        else if (substr == "postitem")
        {
            return "PostItem";
        }
        else if (substr == "conversation")
        {
            return "Conversation";
        }
        // Persona missing
        // else if (substr == "")
        // {
        //     return "";
        // }
        throw exception("Unknown property path");
    }

    virtual std::string to_xml_impl() const
    {
        std::stringstream sstr;
        sstr << "<t:FieldURI FieldURI=\"";
        sstr << uri_ << "\"/>";
        return sstr.str();
    }

    virtual std::string to_xml_impl(const std::string& value) const
    {
        std::stringstream sstr;
        sstr << "<t:FieldURI FieldURI=\"";
        sstr << uri_ << "\"/>";
        sstr << "<t:" << class_name() << ">";
        sstr << "<t:" << property_name() << ">";
        sstr << value;
        sstr << "</t:" << property_name() << ">";
        sstr << "</t:" << class_name() << ">";
        return sstr.str();
    }

private:
    std::string property_name() const
    {
        const auto n = uri_.rfind(':');
        check((n != std::string::npos), "Expected a ':' in URI");
        return uri_.substr(n + 1);
    }

    std::string uri_;
};

inline bool operator==(const property_path& lhs, const property_path& rhs)
{
    return lhs.field_uri() == rhs.field_uri();
}

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<property_path>::value);
static_assert(std::is_copy_constructible<property_path>::value);
static_assert(std::is_copy_assignable<property_path>::value);
static_assert(std::is_move_constructible<property_path>::value);
static_assert(std::is_move_assignable<property_path>::value);
#endif

//! Identifies individual members of a dictionary property by an URI and index
class indexed_property_path final : public property_path
{
public:
    indexed_property_path(const char* uri, const char* index)
        : property_path(uri), index_(index)
    {
    }

private:
    // TODO: why no const char* overload?

    std::string to_xml_impl() const override
    {
        std::stringstream sstr;
        sstr << "<t:IndexedFieldURI FieldURI=";
        sstr << "\"" << field_uri() << "\"";
        sstr << " FieldIndex=";
        sstr << "\"";
        sstr << index_;
        sstr << "\"";
        sstr << "/>";
        return sstr.str();
    }

    std::string to_xml_impl(const std::string& value) const override
    {
        std::stringstream sstr;
        sstr << "<t:IndexedFieldURI FieldURI=";
        sstr << "\"" << field_uri() << "\"";
        sstr << " FieldIndex=";
        sstr << "\"";
        sstr << index_;
        sstr << "\"";
        sstr << "/>";
        sstr << "<t:" << class_name() << ">";
        sstr << value;
        sstr << " </t:" << class_name() << ">";
        return sstr.str();
    }

    std::string index_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<indexed_property_path>::value);
static_assert(std::is_copy_constructible<indexed_property_path>::value);
static_assert(std::is_copy_assignable<indexed_property_path>::value);
static_assert(std::is_move_constructible<indexed_property_path>::value);
static_assert(std::is_move_assignable<indexed_property_path>::value);
#endif

// TODO: extended_property_path missing?

namespace folder_property_path
{
    static const property_path folder_id = "folder:FolderId";
    static const property_path parent_folder_id = "folder:ParentFolderId";
    static const property_path display_name = "folder:DisplayName";
    static const property_path unread_count = "folder:UnreadCount";
    static const property_path total_count = "folder:TotalCount";
    static const property_path child_folder_count = "folder:ChildFolderCount";
    static const property_path folder_class = "folder:FolderClass";
    static const property_path search_parameters = "folder:SearchParameters";
    static const property_path managed_folder_information =
        "folder:ManagedFolderInformation";
    static const property_path permission_set = "folder:PermissionSet";
    static const property_path effective_rights = "folder:EffectiveRights";
    static const property_path sharing_effective_rights =
        "folder:SharingEffectiveRights";
} // namespace folder_property_path

namespace item_property_path
{
    static const property_path item_id = "item:ItemId";
    static const property_path parent_folder_id = "item:ParentFolderId";
    static const property_path item_class = "item:ItemClass";
    static const property_path mime_content = "item:MimeContent";
    static const property_path attachments = "item:Attachments";
    static const property_path subject = "item:Subject";
    static const property_path date_time_received = "item:DateTimeReceived";
    static const property_path size = "item:Size";
    static const property_path categories = "item:Categories";
    static const property_path has_attachments = "item:HasAttachments";
    static const property_path importance = "item:Importance";
    static const property_path in_reply_to = "item:InReplyTo";
    static const property_path internet_message_headers =
        "item:InternetMessageHeaders";
    static const property_path is_associated = "item:IsAssociated";
    static const property_path is_draft = "item:IsDraft";
    static const property_path is_from_me = "item:IsFromMe";
    static const property_path is_resend = "item:IsResend";
    static const property_path is_submitted = "item:IsSubmitted";
    static const property_path is_unmodified = "item:IsUnmodified";
    static const property_path date_time_sent = "item:DateTimeSent";
    static const property_path date_time_created = "item:DateTimeCreated";
    static const property_path body = "item:Body";
    static const property_path response_objects = "item:ResponseObjects";
    static const property_path sensitivity = "item:Sensitivity";
    static const property_path reminder_due_by = "item:ReminderDueBy";
    static const property_path reminder_is_set = "item:ReminderIsSet";
    static const property_path reminder_next_time = "item:ReminderNextTime";
    static const property_path reminder_minutes_before_start =
        "item:ReminderMinutesBeforeStart";
    static const property_path display_to = "item:DisplayTo";
    static const property_path display_cc = "item:DisplayCc";
    static const property_path culture = "item:Culture";
    static const property_path effective_rights = "item:EffectiveRights";
    static const property_path last_modified_name = "item:LastModifiedName";
    static const property_path last_modified_time = "item:LastModifiedTime";
    static const property_path conversation_id = "item:ConversationId";
    static const property_path unique_body = "item:UniqueBody";
    static const property_path flag = "item:Flag";
    static const property_path store_entry_id = "item:StoreEntryId";
    static const property_path instance_key = "item:InstanceKey";
    static const property_path normalized_body = "item:NormalizedBody";
    static const property_path entity_extraction_result =
        "item:EntityExtractionResult";
    static const property_path policy_tag = "item:PolicyTag";
    static const property_path archive_tag = "item:ArchiveTag";
    static const property_path retention_date = "item:RetentionDate";
    static const property_path preview = "item:Preview";
    static const property_path next_predicted_action =
        "item:NextPredictedAction";
    static const property_path grouping_action = "item:GroupingAction";
    static const property_path predicted_action_reasons =
        "item:PredictedActionReasons";
    static const property_path is_clutter = "item:IsClutter";
    static const property_path rights_management_license_data =
        "item:RightsManagementLicenseData";
    static const property_path block_status = "item:BlockStatus";
    static const property_path has_blocked_images = "item:HasBlockedImages";
    static const property_path web_client_read_from_query_string =
        "item:WebClientReadFormQueryString";
    static const property_path web_client_edit_from_query_string =
        "item:WebClientEditFormQueryString";
    static const property_path text_body = "item:TextBody";
    static const property_path icon_index = "item:IconIndex";
    static const property_path mime_content_utf8 = "item:MimeContentUTF8";
} // namespace item_property_path

namespace message_property_path
{
    static const property_path conversation_index = "message:ConversationIndex";
    static const property_path conversation_topic = "message:ConversationTopic";
    static const property_path internet_message_id =
        "message:InternetMessageId";
    static const property_path is_read = "message:IsRead";
    static const property_path is_response_requested =
        "message:IsResponseRequested";
    static const property_path is_read_receipt_requested =
        "message:IsReadReceiptRequested";
    static const property_path is_delivery_receipt_requested =
        "message:IsDeliveryReceiptRequested";
    static const property_path received_by = "message:ReceivedBy";
    static const property_path received_representing =
        "message:ReceivedRepresenting";
    static const property_path references = "message:References";
    static const property_path reply_to = "message:ReplyTo";
    static const property_path from = "message:From";
    static const property_path sender = "message:Sender";
    static const property_path to_recipients = "message:ToRecipients";
    static const property_path cc_recipients = "message:CcRecipients";
    static const property_path bcc_recipients = "message:BccRecipients";
    static const property_path approval_request_data =
        "message:ApprovalRequestData";
    static const property_path voting_information = "message:VotingInformation";
    static const property_path reminder_message_data =
        "message:ReminderMessageData";
} // namespace message_property_path

namespace meeting_property_path
{
    static const property_path associated_calendar_item_id =
        "meeting:AssociatedCalendarItemId";
    static const property_path is_delegated = "meeting:IsDelegated";
    static const property_path is_out_of_date = "meeting:IsOutOfDate";
    static const property_path has_been_processed = "meeting:HasBeenProcessed";
    static const property_path response_type = "meeting:ResponseType";
    static const property_path proposed_start = "meeting:ProposedStart";
    static const property_path proposed_end = "meeting:PropsedEnd";
} // namespace meeting_property_path

namespace meeting_request_property_path
{
    static const property_path meeting_request_type =
        "meetingRequest:MeetingRequestType";
    static const property_path intended_free_busy_status =
        "meetingRequest:IntendedFreeBusyStatus";
    static const property_path change_highlights =
        "meetingRequest:ChangeHighlights";
} // namespace meeting_request_property_path

namespace calendar_property_path
{
    static const property_path start = "calendar:Start";
    static const property_path end = "calendar:End";
    static const property_path original_start = "calendar:OriginalStart";
    static const property_path start_wall_clock = "calendar:StartWallClock";
    static const property_path end_wall_clock = "calendar:EndWallClock";
    static const property_path start_time_zone_id = "calendar:StartTimeZoneId";
    static const property_path end_time_zone_id = "calendar:EndTimeZoneId";
    static const property_path is_all_day_event = "calendar:IsAllDayEvent";
    static const property_path legacy_free_busy_status =
        "calendar:LegacyFreeBusyStatus";
    static const property_path location = "calendar:Location";
    static const property_path when = "calendar:When";
    static const property_path is_meeting = "calendar:IsMeeting";
    static const property_path is_cancelled = "calendar:IsCancelled";
    static const property_path is_recurring = "calendar:IsRecurring";
    static const property_path meeting_request_was_sent =
        "calendar:MeetingRequestWasSent";
    static const property_path is_response_requested =
        "calendar:IsResponseRequested";
    static const property_path calendar_item_type = "calendar:CalendarItemType";
    static const property_path my_response_type = "calendar:MyResponseType";
    static const property_path organizer = "calendar:Organizer";
    static const property_path required_attendees =
        "calendar:RequiredAttendees";
    static const property_path optional_attendees =
        "calendar:OptionalAttendees";
    static const property_path resources = "calendar:Resources";
    static const property_path conflicting_meeting_count =
        "calendar:ConflictingMeetingCount";
    static const property_path adjacent_meeting_count =
        "calendar:AdjacentMeetingCount";
    static const property_path conflicting_meetings =
        "calendar:ConflictingMeetings";
    static const property_path adjacent_meetings = "calendar:AdjacentMeetings";
    static const property_path duration = "calendar:Duration";
    static const property_path time_zone = "calendar:TimeZone";
    static const property_path appointment_reply_time =
        "calendar:AppointmentReplyTime";
    static const property_path appointment_sequence_number =
        "calendar:AppointmentSequenceNumber";
    static const property_path appointment_state = "calendar:AppointmentState";
    static const property_path recurrence = "calendar:Recurrence";
    static const property_path first_occurrence = "calendar:FirstOccurrence";
    static const property_path last_occurrence = "calendar:LastOccurrence";
    static const property_path modified_occurrences =
        "calendar:ModifiedOccurrences";
    static const property_path deleted_occurrences =
        "calendar:DeletedOccurrences";
    static const property_path meeting_time_zone = "calendar:MeetingTimeZone";
    static const property_path conference_type = "calendar:ConferenceType";
    static const property_path allow_new_time_proposal =
        "calendar:AllowNewTimeProposal";
    static const property_path is_online_meeting = "calendar:IsOnlineMeeting";
    static const property_path meeting_workspace_url =
        "calendar:MeetingWorkspaceUrl";
    static const property_path net_show_url = "calendar:NetShowUrl";
    static const property_path uid = "calendar:UID";
    static const property_path recurrence_id = "calendar:RecurrenceId";
    static const property_path date_time_stamp = "calendar:DateTimeStamp";
    static const property_path start_time_zone = "calendar:StartTimeZone";
    static const property_path end_time_zone = "calendar:EndTimeZone";
    static const property_path join_online_meeting_url =
        "calendar:JoinOnlineMeetingUrl";
    static const property_path online_meeting_settings =
        "calendar:OnlineMeetingSettings";
    static const property_path is_organizer = "calendar:IsOrganizer";
} // namespace calendar_property_path

namespace task_property_path
{
    static const property_path actual_work = "task:ActualWork";
    static const property_path assigned_time = "task:AssignedTime";
    static const property_path billing_information = "task:BillingInformation";
    static const property_path change_count = "task:ChangeCount";
    static const property_path companies = "task:Companies";
    static const property_path complete_date = "task:CompleteDate";
    static const property_path contacts = "task:Contacts";
    static const property_path delegation_state = "task:DelegationState";
    static const property_path delegator = "task:Delegator";
    static const property_path due_date = "task:DueDate";
    static const property_path is_assignment_editable =
        "task:IsAssignmentEditable";
    static const property_path is_complete = "task:IsComplete";
    static const property_path is_recurring = "task:IsRecurring";
    static const property_path is_team_task = "task:IsTeamTask";
    static const property_path mileage = "task:Mileage";
    static const property_path owner = "task:Owner";
    static const property_path percent_complete = "task:PercentComplete";
    static const property_path recurrence = "task:Recurrence";
    static const property_path start_date = "task:StartDate";
    static const property_path status = "task:Status";
    static const property_path status_description = "task:StatusDescription";
    static const property_path total_work = "task:TotalWork";
} // namespace task_property_path

namespace contact_property_path
{
    static const property_path alias = "contacts:Alias";
    static const property_path assistant_name = "contacts:AssistantName";
    static const property_path birthday = "contacts:Birthday";
    static const property_path business_home_page = "contacts:BusinessHomePage";
    static const property_path children = "contacts:Children";
    static const property_path companies = "contacts:Companies";
    static const property_path company_name = "contacts:CompanyName";
    static const property_path complete_name = "contacts:CompleteName";
    static const property_path contact_source = "contacts:ContactSource";
    static const property_path culture = "contacts:Culture";
    static const property_path department = "contacts:Department";
    static const property_path display_name = "contacts:DisplayName";
    static const property_path directory_id = "contacts:DirectoryId";
    static const property_path direct_reports = "contacts:DirectReports";
    static const property_path email_addresses = "contacts:EmailAddresses";
    static const property_path email_address = "contacts:EmailAddress";
    static const indexed_property_path email_address_1("contacts:EmailAddress",
                                                       "EmailAddress1");
    static const indexed_property_path email_address_2("contacts:EmailAddress",
                                                       "EmailAddress2");
    static const indexed_property_path email_address_3("contacts:EmailAddress",
                                                       "EmailAddress3");
    static const property_path file_as = "contacts:FileAs";
    static const property_path file_as_mapping = "contacts:FileAsMapping";
    static const property_path generation = "contacts:Generation";
    static const property_path given_name = "contacts:GivenName";
    static const property_path im_addresses = "contacts:ImAddresses";
    static const property_path im_address = "contacts:ImAddress";
    static const indexed_property_path im_address_1("contacts:ImAddress",
                                                    "ImAddress1");
    static const indexed_property_path im_address_2("contacts:ImAddress",
                                                    "ImAddress2");
    static const indexed_property_path im_address_3("contacts:ImAddress",
                                                    "ImAddress3");
    static const property_path initials = "contacts:Initials";
    static const property_path job_title = "contacts:JobTitle";
    static const property_path manager = "contacts:Manager";
    static const property_path manager_mailbox = "contacts:ManagerMailbox";
    static const property_path middle_name = "contacts:MiddleName";
    static const property_path mileage = "contacts:Mileage";
    static const property_path ms_exchange_certificate =
        "contacts:MSExchangeCertificate";
    static const property_path nickname = "contacts:Nickname";
    static const property_path notes = "contacts:Notes";
    static const property_path office_location = "contacts:OfficeLocation";
    static const property_path phone_numbers = "contacts:PhoneNumbers";

    namespace phone_number
    {
        static const indexed_property_path
            assistant_phone("contacts:PhoneNumber", "AssistantPhone");
        static const indexed_property_path business_fax("contacts:PhoneNumber",
                                                        "BusinessFax");
        static const indexed_property_path
            business_phone("contacts:PhoneNumber", "BusinessPhone");
        static const indexed_property_path
            business_phone_2("contacts:PhoneNumber", "BusinessPhone2");
        static const indexed_property_path callback("contacts:PhoneNumber",
                                                    "Callback");
        static const indexed_property_path car_phone("contacts:PhoneNumber",
                                                     "CarPhone");
        static const indexed_property_path
            company_main_phone("contacts:PhoneNumber", "CompanyMainPhone");
        static const indexed_property_path home_fax("contacts:PhoneNumber",
                                                    "HomeFax");
        static const indexed_property_path home_phone("contacts:PhoneNumber",
                                                      "HomePhone");
        static const indexed_property_path home_phone_2("contacts:PhoneNumber",
                                                        "HomePhone2");
        static const indexed_property_path isdn("contacts:PhoneNumber", "Isdn");

        static const indexed_property_path mobile_phone("contacts:PhoneNumber",
                                                        "MobilePhone");
        static const indexed_property_path other_fax("contacts:PhoneNumber",
                                                     "OtherFax");
        static const indexed_property_path
            other_telephone("contacts:PhoneNumber", "OtherTelephone");
        static const indexed_property_path pager("contacts:PhoneNumber",
                                                 "Pager");
        static const indexed_property_path primary_phone("contacts:PhoneNumber",
                                                         "PrimaryPhone");
        static const indexed_property_path radio_phone("contacts:PhoneNumber",
                                                       "RadioPhone");
        static const indexed_property_path telex("contacts:PhoneNumber",
                                                 "Telex");
        static const indexed_property_path tty_tdd_phone("contacts:PhoneNumber",
                                                         "TtyTddPhone");
    } // namespace phone_number

    static const property_path phonetic_full_name = "contacts:PhoneticFullName";
    static const property_path phonetic_first_name =
        "contacts:PhoneticFirstName";
    static const property_path phonetic_last_name = "contacts:PhoneticLastName";
    static const property_path photo = "contacts:Photo";
    static const property_path physical_addresses =
        "contacts:PhysicalAddresses";

    namespace physical_address
    {
        namespace business
        {
            static const indexed_property_path
                street("contacts:PhysicalAddress:Street", "Business");
            static const indexed_property_path
                city("contacts:PhysicalAddress:City", "Business");
            static const indexed_property_path
                state("contacts:PhysicalAddress:State", "Business");
            static const indexed_property_path
                country_or_region("contacts:PhysicalAddress:CountryOrRegion",
                                  "Business");
            static const indexed_property_path
                postal_code("contacts:PhysicalAddress:PostalCode", "Business");
        } // namespace business
        namespace home
        {
            static const indexed_property_path
                street("contacts:PhysicalAddress:Street", "Home");
            static const indexed_property_path
                city("contacts:PhysicalAddress:City", "Home");
            static const indexed_property_path
                state("contacts:PhysicalAddress:State", "Home");
            static const indexed_property_path
                country_or_region("contacts:PhysicalAddress:CountryOrRegion",
                                  "Home");
            static const indexed_property_path
                postal_code("contacts:PhysicalAddress:PostalCode", "Home");
        } // namespace home
        namespace other
        {
            static const indexed_property_path
                street("contacts:PhysicalAddress:Street", "Other");
            static const indexed_property_path
                city("contacts:PhysicalAddress:City", "Other");
            static const indexed_property_path
                state("contacts:PhysicalAddress:State", "Other");
            static const indexed_property_path
                country_or_region("contacts:PhysicalAddress:CountryOrRegion",
                                  "Other");
            static const indexed_property_path
                postal_code("contacts:PhysicalAddress:PostalCode", "Other");
        } // namespace other
    }     // namespace physical_address

    static const property_path postal_address_index =
        "contacts:PostalAddressIndex";
    static const property_path profession = "contacts:Profession";
    static const property_path spouse_name = "contacts:SpouseName";
    static const property_path surname = "contacts:Surname";
    static const property_path wedding_anniversary =
        "contacts:WeddingAnniversary";
    static const property_path smime_certificate =
        "contacts:UserSMIMECertificate";
    static const property_path has_picture = "contacts:HasPicture";
} // namespace contact_property_path

namespace distribution_list_property_path
{
    static const property_path members = "distributionlist:Members";
}

namespace post_item_property_path
{
    static const property_path posted_time = "postitem:PostedTime";
}

namespace conversation_property_path
{
    static const property_path conversation_id = "conversation:ConversationId";
    static const property_path conversation_topic =
        "conversation:ConversationTopic";
    static const property_path unique_recipients =
        "conversation:UniqueRecipients";
    static const property_path global_unique_recipients =
        "conversation:GlobalUniqueRecipients";
    static const property_path unique_unread_senders =
        "conversation:UniqueUnreadSenders";
    static const property_path global_unique_unread_readers =
        "conversation:GlobalUniqueUnreadSenders";
    static const property_path unique_senders = "conversation:UniqueSenders";
    static const property_path global_unique_senders =
        "conversation:GlobalUniqueSenders";
    static const property_path last_delivery_time =
        "conversation:LastDeliveryTime";
    static const property_path global_last_delivery_time =
        "conversation:GlobalLastDeliveryTime";
    static const property_path categories = "conversation:Categories";
    static const property_path global_categories =
        "conversation:GlobalCategories";
    static const property_path flag_status = "conversation:FlagStatus";
    static const property_path global_flag_status =
        "conversation:GlobalFlagStatus";
    static const property_path has_attachments = "conversation:HasAttachments";
    static const property_path global_has_attachments =
        "conversation:GlobalHasAttachments";
    static const property_path has_irm = "conversation:HasIrm";
    static const property_path global_has_irm = "conversation:GlobalHasIrm";
    static const property_path message_count = "conversation:MessageCount";
    static const property_path global_message_count =
        "conversation:GlobalMessageCount";
    static const property_path unread_count = "conversation:UnreadCount";
    static const property_path global_unread_count =
        "conversation:GlobalUnreadCount";
    static const property_path size = "conversation:Size";
    static const property_path global_size = "conversation:GlobalSize";
    static const property_path item_classes = "conversation:ItemClasses";
    static const property_path global_item_classes =
        "conversation:GlobalItemClasses";
    static const property_path importance = "conversation:Importance";
    static const property_path global_importance =
        "conversation:GlobalImportance";
    static const property_path item_ids = "conversation:ItemIds";
    static const property_path global_item_ids = "conversation:GlobalItemIds";
    static const property_path last_modified_time =
        "conversation:LastModifiedTime";
    static const property_path instance_key = "conversation:InstanceKey";
    static const property_path preview = "conversation:Preview";
    static const property_path global_parent_folder_id =
        "conversation:GlobalParentFolderId";
    static const property_path next_predicted_action =
        "conversation:NextPredictedAction";
    static const property_path grouping_action = "conversation:GroupingAction";
    static const property_path icon_index = "conversation:IconIndex";
    static const property_path global_icon_index =
        "conversation:GlobalIconIndex";
    static const property_path draft_item_ids = "conversation:DraftItemIds";
    static const property_path has_clutter = "conversation:HasClutter";
} // namespace conversation_property_path

//! \brief Represents a single property
//!
//! Used in ews::service::update_item method call.
class property final
{
public:
    // TODO: shouldn't the c'tors take const& instead?

    //! Use this constructor if you want to delete a property from an item
    explicit property(property_path path) : value_(path.to_xml()) {}

    //! Use this constructor if you want to delete an indexed property from an
    //! item
    explicit property(indexed_property_path path) : value_(path.to_xml()) {}

    // Use this constructor (and following overloads) whenever you want to
    // set or update an item's property
    property(property_path path, const std::string& value)
        : value_(path.to_xml(internal::escape(value)))
    {
    }

    property(property_path path, const char* value)
        : value_(path.to_xml(internal::escape(value)))
    {
    }

    property(property_path path, int value)
        : value_(path.to_xml(std::to_string(value)))
    {
    }

    property(property_path path, long value)
        : value_(path.to_xml(std::to_string(value)))
    {
    }

    property(property_path path, long long value)
        : value_(path.to_xml(std::to_string(value)))
    {
    }

    property(property_path path, unsigned value)
        : value_(path.to_xml(std::to_string(value)))
    {
    }

    property(property_path path, unsigned long value)
        : value_(path.to_xml(std::to_string(value)))
    {
    }

    property(property_path path, unsigned long long value)
        : value_(path.to_xml(std::to_string(value)))
    {
    }

    property(property_path path, float value)
        : value_(path.to_xml(std::to_string(value)))
    {
    }

    property(property_path path, double value)
        : value_(path.to_xml(std::to_string(value)))
    {
    }

    property(property_path path, long double value)
        : value_(path.to_xml(std::to_string(value)))
    {
    }

    property(property_path path, bool value)
        : value_(path.to_xml((value ? "true" : "false")))
    {
    }

#ifdef EWS_HAS_DEFAULT_TEMPLATE_ARGS_FOR_FUNCTIONS
    template <typename T,
              typename = typename std::enable_if<std::is_enum<T>::value>::type>
    property(property_path path, T enum_value)
        : value_(path.to_xml(internal::enum_to_str(enum_value)))
    {
    }
#else
    property(property_path path, free_busy_status enum_value)
        : value_(path.to_xml(internal::enum_to_str(enum_value)))
    {
    }

    property(property_path path, sensitivity enum_value)
        : value_(path.to_xml(internal::enum_to_str(enum_value)))
    {
    }
#endif

    property(property_path path, const body& value)
        : value_(path.to_xml(value.to_xml()))
    {
    }

    property(property_path path, const date_time& value)
        : value_(path.to_xml(value.to_string()))
    {
    }

    property(property_path path, const mailbox& value)
        : value_(path.to_xml(value.to_xml()))
    {
    }

    property(property_path path, const recurrence_pattern& pattern,
             const recurrence_range& range)
        : value_()
    {
        std::stringstream sstr;
        sstr << "<t:Recurrence>" << pattern.to_xml() << range.to_xml()
             << "</t:Recurrence>";
        value_ = path.to_xml(sstr.str());
    }

    template <typename T>
    property(property_path path, const std::vector<T>& value) : value_()
    {
        std::stringstream sstr;
        for (const auto& elem : value)
        {
            sstr << elem.to_xml();
        }
        value_ = path.to_xml(sstr.str());
    }

    property(property_path path, const std::vector<std::string>& value)
        : value_()
    {
        std::stringstream sstr;
        for (const auto& str : value)
        {
            sstr << "<t:String>" << str << "</t:String>";
        }
        value_ = path.to_xml(sstr.str());
    }

    property(const indexed_property_path& path, const physical_address& address)
        : value_(path.to_xml(address.to_xml()))
    {
    }

    property(const indexed_property_path& path, const im_address& address)
        : value_(path.to_xml(address.to_xml()))
    {
    }

    property(const indexed_property_path& path, const email_address& address)
        : value_(path.to_xml(address.to_xml()))
    {
    }

    property(const indexed_property_path& path, const phone_number& number)
        : value_(path.to_xml(number.to_xml()))
    {
    }

    const std::string& to_xml() const EWS_NOEXCEPT { return value_; }

private:
    std::string value_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<property>::value);
static_assert(std::is_copy_constructible<property>::value);
static_assert(std::is_copy_assignable<property>::value);
static_assert(std::is_move_constructible<property>::value);
static_assert(std::is_move_assignable<property>::value);
#endif

//! Renders an \<ItemShape> element
class item_shape final
{
public:
    item_shape()
        : base_shape_(base_shape::default_shape), body_type_(body_type::best),
          filter_html_content_(false), include_mime_content_(false),
          convert_html_code_page_to_utf8_(true)
    {
    }

    item_shape(base_shape shape) // intentionally not explicit
        : base_shape_(shape), body_type_(body_type::best),
          filter_html_content_(false), include_mime_content_(false),
          convert_html_code_page_to_utf8_(true)
    {
    }

    item_shape(base_shape shape,
               std::vector<extended_field_uri>&& extended_field_uris)
        : base_shape_(shape), body_type_(body_type::best),
          extended_field_uris_(std::move(extended_field_uris)),
          filter_html_content_(false), include_mime_content_(false),
          convert_html_code_page_to_utf8_(true)
    {
    }

    explicit item_shape(std::vector<property_path>&& additional_properties)
        : base_shape_(base_shape::default_shape), body_type_(body_type::best),
          additional_properties_(std::move(additional_properties)),
          filter_html_content_(false), include_mime_content_(false),
          convert_html_code_page_to_utf8_(true)
    {
    }

    explicit item_shape(std::vector<extended_field_uri>&& extended_field_uris)
        : base_shape_(base_shape::default_shape), body_type_(body_type::best),
          extended_field_uris_(std::move(extended_field_uris)),
          filter_html_content_(false), include_mime_content_(false),
          convert_html_code_page_to_utf8_(true)
    {
    }

    item_shape(std::vector<property_path>&& additional_properties,
               std::vector<extended_field_uri>&& extended_field_uris)
        : base_shape_(base_shape::default_shape), body_type_(body_type::best),
          additional_properties_(std::move(additional_properties)),
          extended_field_uris_(std::move(extended_field_uris)),
          filter_html_content_(false), include_mime_content_(false),
          convert_html_code_page_to_utf8_(true)
    {
    }

    std::string to_xml() const
    {
        std::stringstream sstr;

        sstr << "<m:ItemShape>"

                "<t:BaseShape>";
        sstr << internal::enum_to_str(base_shape_);
        sstr << "</t:BaseShape>"

                "<t:BodyType>";
        sstr << internal::body_type_str(body_type_);
        sstr << "</t:BodyType>"

                "<t:AdditionalProperties>";
        for (const auto& prop : additional_properties_)
        {
            sstr << prop.to_xml();
        }
        for (auto field : extended_field_uris_)
        {
            sstr << field.to_xml();
        }
        sstr << "</t:AdditionalProperties>"

                "<t:FilterHtmlContent>";
        sstr << (filter_html_content_ ? "true" : "false");
        sstr << "</t:FilterHtmlContent>"

                "<t:IncludeMimeContent>";
        sstr << (include_mime_content_ ? "true" : "false");
        sstr << "</t:IncludeMimeContent>"

                "<t:ConvertHtmlCodePageToUTF8>";
        sstr << (convert_html_code_page_to_utf8_ ? "true" : "false");
        sstr << "</t:ConvertHtmlCodePageToUTF8>"

                "</m:ItemShape>";

        return sstr.str();
    }

    base_shape get_base_shape() const EWS_NOEXCEPT { return base_shape_; }

    body_type get_body_type() const EWS_NOEXCEPT { return body_type_; }

    const std::vector<property_path>&
    get_additional_properties() const EWS_NOEXCEPT
    {
        return additional_properties_;
    }

    const std::vector<extended_field_uri>&
    get_extended_field_uris() const EWS_NOEXCEPT
    {
        return extended_field_uris_;
    }

    bool has_filter_html_content() const EWS_NOEXCEPT
    {
        return filter_html_content_;
    }

    bool has_include_mime_content() const EWS_NOEXCEPT
    {
        return include_mime_content_;
    }

    bool has_convert_html_code_page_to_utf8() const EWS_NOEXCEPT
    {
        return convert_html_code_page_to_utf8_;
    }

    void set_base_shape(base_shape base_shape) { base_shape_ = base_shape; }

    void set_body_type(body_type body_type) { body_type_ = body_type; }

    void set_filter_html_content(bool filter_html_content)
    {
        filter_html_content_ = filter_html_content;
    }

    void set_include_mime_content(bool include_mime_content)
    {
        include_mime_content_ = include_mime_content;
    }

    void
    set_convert_html_code_page_to_utf8_(bool convert_html_code_page_to_utf8)
    {
        convert_html_code_page_to_utf8_ = convert_html_code_page_to_utf8;
    }

private:
    base_shape base_shape_;
    body_type body_type_;
    std::vector<property_path> additional_properties_;
    std::vector<extended_field_uri> extended_field_uris_;
    bool filter_html_content_;
    bool include_mime_content_;
    bool convert_html_code_page_to_utf8_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<item_shape>::value);
static_assert(std::is_copy_constructible<item_shape>::value);
static_assert(std::is_copy_assignable<item_shape>::value);
static_assert(std::is_move_constructible<item_shape>::value);
static_assert(std::is_move_assignable<item_shape>::value);
#endif

//! \brief Base-class for all search expressions.
//!
//! Search expressions are used to restrict the result set of a
//! \<FindItem/> operation.
//!
//! E.g.
//!
//!   - exists
//!   - excludes
//!   - is_equal_to
//!   - is_not_equal_to
//!   - is_greater_than
//!   - is_greater_than_or_equal_to
//!   - is_less_than
//!   - is_less_than_or_equal_to
//!   - contains
//!   - not
//!   - and
//!   - or
class search_expression
{
public:
#ifdef EWS_HAS_DEFAULT_AND_DELETE
    search_expression() = delete;
#endif

    std::string to_xml() const { return func_(); }

protected:
    explicit search_expression(std::function<std::string()>&& func)
        : func_(std::move(func))
    {
    }

    // Used by sub-classes to share common code
    search_expression(const char* term, property_path path, bool b)
        : func_([=]() -> std::string {
              std::stringstream sstr;

              sstr << "<t:" << term << ">";
              sstr << path.to_xml();
              sstr << "<t:FieldURIOrConstant>";
              sstr << "<t:Constant Value=\"";
              sstr << std::boolalpha << b;
              sstr << "\"/></t:FieldURIOrConstant></t:";
              sstr << term << ">";
              return sstr.str();
          })
    {
    }

    search_expression(const char* term, property_path path, int i)
        : func_([=]() -> std::string {
              std::stringstream sstr;

              sstr << "<t:" << term << ">";
              sstr << path.to_xml();
              sstr << "<t:"
                   << "FieldURIOrConstant>";
              sstr << "<t:Constant Value=\"";
              sstr << std::to_string(i);
              sstr << "\"/></t:FieldURIOrConstant></t:";
              sstr << term << ">";
              return sstr.str();
          })
    {
    }

    search_expression(const char* term, property_path path, const char* str)
        : func_([=]() -> std::string {
              std::stringstream sstr;

              sstr << "<t:" << term << ">";
              sstr << path.to_xml();
              sstr << "<t:"
                   << "FieldURIOrConstant>";
              sstr << "<t:Constant Value=\"";
              sstr << str;
              sstr << "\"/></t:FieldURIOrConstant></t:";
              sstr << term << ">";
              return sstr.str();
          })
    {
    }

    search_expression(const char* term, indexed_property_path path,
                      const char* str)
        : func_([=]() -> std::string {
              std::stringstream sstr;

              sstr << "<t:" << term << ">";
              sstr << path.to_xml();
              sstr << "<t:FieldURIOrConstant>";
              sstr << "<t:Constant Value=\"";
              sstr << str;
              sstr << "\"/></t:FieldURIOrConstant></t:";
              sstr << term << ">";
              return sstr.str();
          })
    {
    }

    search_expression(const char* term, property_path path, date_time when)
        : func_([=]() -> std::string {
              std::stringstream sstr;

              sstr << "<t:" << term << ">";
              sstr << path.to_xml();
              sstr << "<t:FieldURIOrConstant>";
              sstr << "<t:Constant Value=\"";
              sstr << when.to_string();
              sstr << "\"/></t:FieldURIOrConstant></t:";
              sstr << term << ">";
              return sstr.str();
          })
    {
    }

private:
    std::function<std::string()> func_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<search_expression>::value);
static_assert(std::is_copy_constructible<search_expression>::value);
static_assert(std::is_copy_assignable<search_expression>::value);
static_assert(std::is_move_constructible<search_expression>::value);
static_assert(std::is_move_assignable<search_expression>::value);
#endif

//! \brief Compare a property with a constant or another property
//!
//! A search expression that compares a property with either a constant
//! value or another property and evaluates to true if they are equal.
class is_equal_to final : public search_expression
{
public:
    is_equal_to(property_path path, bool b)
        : search_expression("IsEqualTo", std::move(path), b)
    {
    }

    is_equal_to(property_path path, int i)
        : search_expression("IsEqualTo", std::move(path), i)
    {
    }

    is_equal_to(property_path path, const char* str)
        : search_expression("IsEqualTo", std::move(path), str)
    {
    }

    is_equal_to(indexed_property_path path, const char* str)
        : search_expression("IsEqualTo", std::move(path), str)
    {
    }

    is_equal_to(property_path path, date_time when)
        : search_expression("IsEqualTo", std::move(path), std::move(when))
    {
    }

    // TODO: is_equal_to(property_path, property_path) {}
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<is_equal_to>::value);
static_assert(std::is_copy_constructible<is_equal_to>::value);
static_assert(std::is_copy_assignable<is_equal_to>::value);
static_assert(std::is_move_constructible<is_equal_to>::value);
static_assert(std::is_move_assignable<is_equal_to>::value);
#endif

//! \brief Compare a property with a constant or another property.
//!
//! Compares a property with either a constant value or another property
//! and returns true if the values are not the same.
class is_not_equal_to final : public search_expression
{
public:
    is_not_equal_to(property_path path, bool b)
        : search_expression("IsNotEqualTo", std::move(path), b)
    {
    }

    is_not_equal_to(property_path path, int i)
        : search_expression("IsNotEqualTo", std::move(path), i)
    {
    }

    is_not_equal_to(property_path path, const char* str)
        : search_expression("IsNotEqualTo", std::move(path), str)
    {
    }

    is_not_equal_to(indexed_property_path path, const char* str)
        : search_expression("IsNotEqualTo", std::move(path), str)
    {
    }

    is_not_equal_to(property_path path, date_time when)
        : search_expression("IsNotEqualTo", std::move(path), std::move(when))
    {
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<is_not_equal_to>::value);
static_assert(std::is_copy_constructible<is_not_equal_to>::value);
static_assert(std::is_copy_assignable<is_not_equal_to>::value);
static_assert(std::is_move_constructible<is_not_equal_to>::value);
static_assert(std::is_move_assignable<is_not_equal_to>::value);
#endif

//! \brief Compare a property with a constant or another property
class is_greater_than final : public search_expression
{
public:
    is_greater_than(property_path path, bool b)
        : search_expression("IsGreaterThan", std::move(path), b)
    {
    }

    is_greater_than(property_path path, int i)
        : search_expression("IsGreaterThan", std::move(path), i)
    {
    }

    is_greater_than(property_path path, const char* str)
        : search_expression("IsGreaterThan", std::move(path), str)
    {
    }

    is_greater_than(indexed_property_path path, const char* str)
        : search_expression("IsGreaterThan", std::move(path), str)
    {
    }

    is_greater_than(property_path path, date_time when)
        : search_expression("IsGreaterThan", std::move(path), std::move(when))
    {
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<is_greater_than>::value);
static_assert(std::is_copy_constructible<is_greater_than>::value);
static_assert(std::is_copy_assignable<is_greater_than>::value);
static_assert(std::is_move_constructible<is_greater_than>::value);
static_assert(std::is_move_assignable<is_greater_than>::value);
#endif

//! \brief Compare a property with a constant or another property
class is_greater_than_or_equal_to final : public search_expression
{
public:
    is_greater_than_or_equal_to(property_path path, bool b)
        : search_expression("IsGreaterThanOrEqualTo", std::move(path), b)
    {
    }

    is_greater_than_or_equal_to(property_path path, int i)
        : search_expression("IsGreaterThanOrEqualTo", std::move(path), i)
    {
    }

    is_greater_than_or_equal_to(property_path path, const char* str)
        : search_expression("IsGreaterThanOrEqualTo", std::move(path), str)
    {
    }

    is_greater_than_or_equal_to(indexed_property_path path, const char* str)
        : search_expression("IsGreaterThanOrEqualTo", std::move(path), str)
    {
    }

    is_greater_than_or_equal_to(property_path path, date_time when)
        : search_expression("IsGreaterThanOrEqualTo", std::move(path),
                            std::move(when))
    {
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(
    !std::is_default_constructible<is_greater_than_or_equal_to>::value);
static_assert(std::is_copy_constructible<is_greater_than_or_equal_to>::value);
static_assert(std::is_copy_assignable<is_greater_than_or_equal_to>::value);
static_assert(std::is_move_constructible<is_greater_than_or_equal_to>::value);
static_assert(std::is_move_assignable<is_greater_than_or_equal_to>::value);
#endif

//! \brief Compare a property with a constant or another property
class is_less_than final : public search_expression
{
public:
    is_less_than(property_path path, bool b)
        : search_expression("IsLessThan", std::move(path), b)
    {
    }

    is_less_than(property_path path, int i)
        : search_expression("IsLessThan", std::move(path), i)
    {
    }

    is_less_than(property_path path, const char* str)
        : search_expression("IsLessThan", std::move(path), str)
    {
    }

    is_less_than(indexed_property_path path, const char* str)
        : search_expression("IsLessThan", std::move(path), str)
    {
    }

    is_less_than(property_path path, date_time when)
        : search_expression("IsLessThan", std::move(path), std::move(when))
    {
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<is_less_than>::value);
static_assert(std::is_copy_constructible<is_less_than>::value);
static_assert(std::is_copy_assignable<is_less_than>::value);
static_assert(std::is_move_constructible<is_less_than>::value);
static_assert(std::is_move_assignable<is_less_than>::value);
#endif

//! \brief Compare a property with a constant or another property
class is_less_than_or_equal_to final : public search_expression
{
public:
    is_less_than_or_equal_to(property_path path, bool b)
        : search_expression("IsLessThanOrEqualTo", std::move(path), b)
    {
    }

    is_less_than_or_equal_to(property_path path, int i)
        : search_expression("IsLessThanOrEqualTo", std::move(path), i)
    {
    }

    is_less_than_or_equal_to(property_path path, const char* str)
        : search_expression("IsLessThanOrEqualTo", std::move(path), str)
    {
    }

    is_less_than_or_equal_to(indexed_property_path path, const char* str)
        : search_expression("IsLessThanOrEqualTo", std::move(path), str)
    {
    }

    is_less_than_or_equal_to(property_path path, date_time when)
        : search_expression("IsLessThanOrEqualTo", std::move(path),
                            std::move(when))
    {
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<is_less_than_or_equal_to>::value);
static_assert(std::is_copy_constructible<is_less_than_or_equal_to>::value);
static_assert(std::is_copy_assignable<is_less_than_or_equal_to>::value);
static_assert(std::is_move_constructible<is_less_than_or_equal_to>::value);
static_assert(std::is_move_assignable<is_less_than_or_equal_to>::value);
#endif

//! \brief Allows you to express a boolean And operation between two search
//! expressions
class and_ final : public search_expression
{
public:
    and_(const search_expression& first, const search_expression& second)
        : search_expression([=]() -> std::string {
              std::stringstream sstr;

              sstr << "<t:And"
                   << ">";
              sstr << first.to_xml();
              sstr << second.to_xml();
              sstr << "</t:And>";
              return sstr.str();
          })
    {
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<and_>::value);
static_assert(std::is_copy_constructible<and_>::value);
static_assert(std::is_copy_assignable<and_>::value);
static_assert(std::is_move_constructible<and_>::value);
static_assert(std::is_move_assignable<and_>::value);
#endif

//! \brief Allows you to express a logical Or operation between two search
//! expressions
class or_ final : public search_expression
{
public:
    or_(const search_expression& first, const search_expression& second)
        : search_expression([=]() -> std::string {
              std::stringstream sstr;

              sstr << "<t:Or"
                   << ">";
              sstr << first.to_xml();
              sstr << second.to_xml();
              sstr << "</t:Or>";
              return sstr.str();
          })
    {
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<or_>::value);
static_assert(std::is_copy_constructible<or_>::value);
static_assert(std::is_copy_assignable<or_>::value);
static_assert(std::is_move_constructible<or_>::value);
static_assert(std::is_move_assignable<or_>::value);
#endif

//! Negates the boolean value of the search expression it contains
class not_ final : public search_expression
{
public:
    not_(const search_expression& expr)
        : search_expression([=]() -> std::string {
              std::stringstream sstr;

              sstr << "<t:Not"
                   << ">";
              sstr << expr.to_xml();
              sstr << "</t:Not>";
              return sstr.str();
          })
    {
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<not_>::value);
static_assert(std::is_copy_constructible<not_>::value);
static_assert(std::is_copy_assignable<not_>::value);
static_assert(std::is_move_constructible<not_>::value);
static_assert(std::is_move_assignable<not_>::value);
#endif

//! \brief Specifies which parts of a text value are compared to a
//! supplied constant value.
//!
//! \sa contains
enum class containment_mode
{
    //! \brief The comparison is between the full string and the constant.
    //!
    //! The property value and the supplied constant are exactly the same.
    full_string,

    //! The comparison is between the string prefix and the constant
    prefixed,

    //! \brief The comparison is between a sub-string of the string and the
    //! constant.
    substring,

    //! \brief The comparison is between a prefix on individual words in
    //! the string and the constant.
    prefix_on_words,

    //! \brief The comparison is between an exact phrase in the string and
    //! the constant.
    exact_phrase
};

namespace internal
{
    inline std::string enum_to_str(containment_mode val)
    {
        switch (val)
        {
        case containment_mode::full_string:
            return "FullString";
        case containment_mode::prefixed:
            return "Prefixed";
        case containment_mode::substring:
            return "Substring";
        case containment_mode::prefix_on_words:
            return "PrefixOnWords";
        case containment_mode::exact_phrase:
            return "ExactPhrase";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! \brief This enumeration determines how case and non-spacing characters
//! are considered when evaluating a text search expression.
//!
//! \sa contains
enum class containment_comparison
{
    //! The strings must exactly be the same
    exact,

    //! The comparison is case-insensitive
    ignore_case,

    //! Non-spacing characters will be ignored during comparison
    ignore_non_spacing_characters,

    //! This is \ref containment_comparison::ignore_case and \ref
    //! containment_comparison::ignore_non_spacing_characters
    loose,
};

namespace internal
{
    inline std::string enum_to_str(containment_comparison val)
    {
        switch (val)
        {
        case containment_comparison::exact:
            return "Exact";
        case containment_comparison::ignore_case:
            return "IgnoreCase";
        case containment_comparison::ignore_non_spacing_characters:
            return "IgnoreNonSpacingCharacters";
        case containment_comparison::loose:
            return "Loose";
        default:
            throw exception("Bad enum value");
        }
    }
} // namespace internal

//! \brief Check if a text property contains a sub-string.
//!
//! A search filter that allows you to do text searches on string properties.
class contains final : public search_expression
{
public:
    contains(property_path path, const char* str,
             containment_mode mode = containment_mode::substring,
             containment_comparison comparison = containment_comparison::loose)
        : search_expression([=]() -> std::string {
              std::stringstream sstr;

              sstr << "<t:Contains ";
              sstr << "ContainmentMode=\"" << internal::enum_to_str(mode)
                   << "\" ";
              sstr << "ContainmentComparison=\""
                   << internal::enum_to_str(comparison) << "\">";
              sstr << path.to_xml();
              sstr << "<t:Constant Value=\"";
              sstr << str;
              sstr << "\"/></t:Contains>";
              return sstr.str();
          })
    {
    }
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<contains>::value);
static_assert(std::is_copy_constructible<contains>::value);
static_assert(std::is_copy_assignable<contains>::value);
static_assert(std::is_move_constructible<contains>::value);
static_assert(std::is_move_assignable<contains>::value);
#endif

//! \brief A paged view of items in a folder
//!
//! Represents a paged view of items in item search operations.
class paging_view final
{
public:
    paging_view()
        : max_entries_returned_(1000), offset_(0),
          base_point_(paging_base_point::beginning)
    {
    }

    paging_view(uint32_t max_entries_returned)
        : max_entries_returned_(max_entries_returned), offset_(0),
          base_point_(paging_base_point::beginning)
    {
    }

    paging_view(uint32_t max_entries_returned, uint32_t offset)
        : max_entries_returned_(max_entries_returned), offset_(offset),
          base_point_(paging_base_point::beginning)
    {
    }

    paging_view(uint32_t max_entries_returned, uint32_t offset,
                paging_base_point base_point)
        : max_entries_returned_(max_entries_returned), offset_(offset),
          base_point_(base_point)
    {
    }

    uint32_t get_max_entries_returned() const EWS_NOEXCEPT
    {
        return max_entries_returned_;
    }

    uint32_t get_offset() const EWS_NOEXCEPT { return offset_; }

    std::string to_xml() const
    {
        return "<m:IndexedPageItemView MaxEntriesReturned=\"" +
               std::to_string(max_entries_returned_) + "\" Offset=\"" +
               std::to_string(offset_) + "\" BasePoint=\"" +
               internal::enum_to_str(base_point_) + "\" />";
    }

    void advance() { offset_ += max_entries_returned_; }

private:
    uint32_t max_entries_returned_;
    uint32_t offset_;
    paging_base_point base_point_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(std::is_default_constructible<paging_view>::value);
static_assert(std::is_copy_constructible<paging_view>::value);
static_assert(std::is_copy_assignable<paging_view>::value);
static_assert(std::is_move_constructible<paging_view>::value);
static_assert(std::is_move_assignable<paging_view>::value);
#endif

//! \brief A range view of appointments in a calendar.
//!
//! Represents a date range view of appointments in calendar folder search
//! operations.
class calendar_view
{
public:
    calendar_view(date_time start_date, date_time end_date)
        : start_date_(std::move(start_date)), end_date_(std::move(end_date)),
          max_entries_returned_(0U), max_entries_set_(false)
    {
    }

    calendar_view(date_time start_date, date_time end_date,
                  uint32_t max_entries_returned)
        : start_date_(std::move(start_date)), end_date_(std::move(end_date)),
          max_entries_returned_(max_entries_returned), max_entries_set_(true)
    {
    }

    uint32_t get_max_entries_returned() const EWS_NOEXCEPT
    {
        return max_entries_returned_;
    }

    const date_time& get_start_date() const EWS_NOEXCEPT { return start_date_; }

    const date_time& get_end_date() const EWS_NOEXCEPT { return end_date_; }

    std::string to_xml() const
    {
        std::stringstream sstr;

        sstr << "<m:CalendarView ";
        if (max_entries_set_)
        {
            sstr << "MaxEntriesReturned=\""
                 << std::to_string(max_entries_returned_) << "\" ";
        }
        sstr << "StartDate=\"" << start_date_.to_string() << "\" EndDate=\""
             << end_date_.to_string() << "\" />";
        return sstr.str();
    }

private:
    date_time start_date_;
    date_time end_date_;
    uint32_t max_entries_returned_;
    bool max_entries_set_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<calendar_view>::value);
static_assert(std::is_copy_constructible<calendar_view>::value);
static_assert(std::is_copy_assignable<calendar_view>::value);
static_assert(std::is_move_constructible<calendar_view>::value);
static_assert(std::is_move_assignable<calendar_view>::value);
#endif

//! \brief An update to a single property of an item.
//!
//! Represents either a \<SetItemField>, an \<AppendToItemField>, or a
//! \<DeleteItemField> operation.
class update final
{
public:
    enum class operation
    {
        //! \brief Replaces or creates a property.
        //!
        //! Replaces data for a property if the property already exists,
        //! otherwise creates the property and sets its value. The
        //! operation is only applicable to read-write properties.
        set_item_field,

        //! \brief Adds data to an existing property.
        //!
        //! This works only on some properties, such as
        //! \li calendar:OptionalAttendees
        //! \li calendar:RequiredAttendees
        //! \li calendar:Resources
        //! \li item:Body
        //! \li message:ToRecipients
        //! \li message:CcRecipients
        //! \li message:BccRecipients
        //! \li message:ReplyTo
        append_to_item_field,

        //! \brief Removes a property from an item.
        //!
        //! Only applicable to read-write properties.
        delete_item_field,
    };

#ifdef EWS_HAS_DEFAULT_AND_DELETE
    update() = delete;
#endif

    // intentionally not explicit
    update(property prop, operation action = operation::set_item_field)
        : prop_(std::move(prop.to_xml())), op_(std::move(action))
    {
    }

    update(extended_property prop, operation action = operation::set_item_field)
        : prop_(prop.to_xml()), op_(std::move(action))
    {
    }

    //! Serializes this update instance to an XML string for item operations
    std::string to_item_xml() const
    {
        std::string action = "SetItemField";
        if (op_ == operation::append_to_item_field)
        {
            action = "AppendToItemField";
        }
        else if (op_ == operation::delete_item_field)
        {
            action = "DeleteItemField";
        }
        std::stringstream sstr;
        sstr << "<t:" << action << ">";
        sstr << prop_;
        sstr << "</t:" << action << ">";
        return sstr.str();
    }

    //! Serializes this update instance to an XML string for folder operations
    std::string to_folder_xml() const
    {
        std::string action = "SetFolderField";
        if (op_ == operation::append_to_item_field)
        {
            action = "AppendToFolderField";
        }
        else if (op_ == operation::delete_item_field)
        {
            action = "DeleteFolderField";
        }
        std::stringstream sstr;
        sstr << "<t:" << action << ">";
        sstr << prop_;
        sstr << "</t:" << action << ">";
        return sstr.str();
    }

private:
    std::string prop_;
    update::operation op_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<update>::value);
static_assert(std::is_copy_constructible<update>::value);
static_assert(std::is_copy_assignable<update>::value);
static_assert(std::is_move_constructible<update>::value);
static_assert(std::is_move_assignable<update>::value);
#endif

//! Represents a ConnectingSID element
class connecting_sid final
{
public:
    enum class type
    {
        principal_name,
        sid,
        primary_smtp_address,
        smtp_address
    };

#ifdef EWS_HAS_DEFAULT_AND_DELETE
    connecting_sid() = delete;
#endif

    //! Constructs a new ConnectingSID element
    inline connecting_sid(type, const std::string&); // implemented below

    //! Serializes this ConnectingSID instance to an XML string
    const std::string& to_xml() const EWS_NOEXCEPT { return xml_; }

private:
    std::string xml_;
};

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<connecting_sid>::value);
static_assert(std::is_copy_constructible<connecting_sid>::value);
static_assert(std::is_copy_assignable<connecting_sid>::value);
static_assert(std::is_move_constructible<connecting_sid>::value);
static_assert(std::is_move_assignable<connecting_sid>::value);
#endif

namespace internal
{
    inline std::string enum_to_str(connecting_sid::type type)
    {
        switch (type)
        {
        case connecting_sid::type::principal_name:
            return "PrincipalName";
        case connecting_sid::type::sid:
            return "SID";
        case connecting_sid::type::primary_smtp_address:
            return "PrimarySmtpAddress";
        case connecting_sid::type::smtp_address:
            return "SmtpAddress";
        default:
            throw exception("Unrecognized ConnectingSID");
        }
    }
} // namespace internal

connecting_sid::connecting_sid(connecting_sid::type t, const std::string& id)
    : xml_()
{
    const auto sid = internal::enum_to_str(t);
    xml_ = "<t:ConnectingSID><t:" + sid + ">" + id + "</t:" + sid +
           "></t:ConnectingSID>";
}

//! Available SSL optons.
//! See https://curl.haxx.se/libcurl/c/CURLOPT_SSL_OPTIONS.html
enum class ssl_options : long
{
    none = 0L,
    allow_beast = CURLSSLOPT_ALLOW_BEAST
#if LIBCURL_VERSION_NUM >= 0x072C00
    ,
    no_revoke = CURLSSLOPT_NO_REVOKE
#endif
#if LIBCURL_VERSION_NUM >= 0x074400
    ,
    no_partialchain = CURLSSLOPT_NO_PARTIALCHAIN
#endif
#if LIBCURL_VERSION_NUM >= 0x074600
    ,
    revoke_best_effort = CURLSSLOPT_REVOKE_BEST_EFFORT
#endif
#if LIBCURL_VERSION_NUM >= 0x074700
    ,
    native_ca = CURLSSLOPT_NATIVE_CA
#endif
};

inline ssl_options operator|(const ssl_options lhs, const ssl_options rhs)
{
    return static_cast<ssl_options>(
        static_cast<std::underlying_type<ssl_options>::type>(lhs) |
        static_cast<std::underlying_type<ssl_options>::type>(rhs));
}

inline bool operator==(const ssl_options lhs, const ssl_options rhs)
{
    return static_cast<std::underlying_type<ssl_options>::type>(lhs) ==
           static_cast<std::underlying_type<ssl_options>::type>(rhs);
}

inline bool operator!=(const ssl_options lhs, const ssl_options rhs)
{
    return static_cast<std::underlying_type<ssl_options>::type>(lhs) !=
           static_cast<std::underlying_type<ssl_options>::type>(rhs);
}

//! \brief Allows you to perform operations on an Exchange server.
//!
//! A service object is used to establish a connection to an Exchange server,
//! authenticate against it, and make one, or more likely, multiple API calls to
//! it, called operations. The basic_service class provides all operations that
//! can be performed on an Exchange server as public member-functions, e.g.,
//!
//! - create_item (\<CreateItem>)
//! - delete_item (\<DeleteItem>)
//! - find_item (\<FindItem>)
//! - send_item (\<SendItem>)
//! - update_item (\<UpdateItem>)
//! - delete_calendar_item (\<DeleteItem>)
//! - delete_contact (\<DeleteItem>)
//! - delete_message (\<DeleteItem>)
//! - delete_task (\<DeleteItem>)
//! - get_calendar_item (\<GetItem>)
//! - get_contact (\<GetItem>)
//! - get_message (\<GetItem>)
//! - get_task (\<GetItem>)
//! - add_delegate (\<AddDelegate>)
//! - get_delegate (\<GetDelegate>)
//! - create_attachment (\<CreateAttachment>)
//! - delete_attachment (\<DeleteAttachment>)
//! - get_attachment (\<GetAttachment>)
//! - create_folder (\<CreateFolder>)
//! - delete_folder (\<DeleteFolder>)
//! - find_folder (\<FindFolder>)
//! - get_folder (\<GetFolder>)
//!
//! to name a few.
//!
//! ## General Usage
//! Usually you want to create one service instance and keep it alive as long
//! as you need a connection to the Exchange server. A TCP connection is
//! established as soon as you make the first call to the server. That TCP
//! connection is kept alive as long as the instance is around. Upon
//! destruction, the TCP connection is closed.
//!
//! While you _can_ create a new service object for each call to, lets say
//! create_item, it is not encouraged to do so because with every new service
//! instance you construct you'd create a new TCP connection and authenticate to
//! the server again which would imply a great deal of overhead for just a
//! single API call. Instead, try to re-use a service object for as many calls
//! to the API as possible.
//!
//! ## Thread Safety
//! Instances of this class are re-entrant but not thread safe. This means that
//! you should not share references to service instances across threads without
//! providing synchronization but it is totally safe to have multiple distinct
//! service instances in different threads.
template <typename RequestHandler = internal::http_request>
class basic_service final
{
public:
    //! \brief Callback for debug messages
    typedef std::function<void(const std::string&)> debug_callback;

#ifdef EWS_HAS_DEFAULT_AND_DELETE
    basic_service() = delete;
#endif

    //! \brief Constructs a new service with given credentials to a server
    //! specified by \p server_uri.
    //!
    //! This constructor will always use NTLM authentication.
    basic_service(const std::string& server_uri, const std::string& domain,
                  const std::string& username, const std::string& password)
        : request_handler_(server_uri), server_version_("Exchange2013_SP1"),
          impersonation_(), time_zone_(time_zone::none)
    {
        request_handler_.set_method(RequestHandler::method::POST);
        request_handler_.set_content_type("text/xml; charset=utf-8");
        ntlm_credentials creds(username, password, domain);
        set_credentials(creds);
    }

    //! \brief Constructs a new service with given credentials to a server
    //! specified by \p server_uri
    basic_service(const std::string& server_uri,
                  const internal::credentials& creds)
        : request_handler_(server_uri), server_version_("Exchange2013_SP1"),
          impersonation_(), time_zone_(time_zone::none)
    {
        request_handler_.set_method(RequestHandler::method::POST);
        request_handler_.set_content_type("text/xml; charset=utf-8");
        set_credentials(creds);
    }

    //! \brief Constructs a new service with given credentials to a server
    //! specified by \p server_uri using the path to folder with CA
    //! certificates
    basic_service(const std::string& server_uri,
                  const internal::credentials& creds,
                  const std::string& caPath)
        : request_handler_(server_uri), server_version_("Exchange2013_SP1"),
          impersonation_(), time_zone_(time_zone::none)
    {
        request_handler_.set_method(RequestHandler::method::POST);
        request_handler_.set_content_type("text/xml; charset=utf-8");
        set_capath(caPath);
        set_credentials(creds);
    }

    //! \brief Constructs a new service with given credentials to a server
    //! specified by \p server_uri
    basic_service(const std::string& server_uri,
                  const internal::credentials& creds, const std::string& cainfo,
                  const std::string& capath, const std::string& proxy_uri,
                  const bool is_http_proxy_tunneling,
                  const debug_callback& callback,
                  const ssl_options ssl_opts = ssl_options::none)
        : request_handler_(server_uri), server_version_("Exchange2013_SP1"),
          impersonation_(), time_zone_(time_zone::none)
    {
        if (!cainfo.empty())
        {
            set_cainfo(cainfo);
        }

        if (!capath.empty())
        {
            set_capath(capath);
        }

        if (!proxy_uri.empty())
        {
            set_proxy(proxy_uri);
        }

        if (is_http_proxy_tunneling)
        {
            set_http_proxy_tunnel(is_http_proxy_tunneling);
        }

        if (callback)
        {
            set_debug_callback(callback);
        }

        if (
#if defined _WIN32 && (_MSC_VER == 1700 || _MSC_VER == 1800)
            static_cast<long>(ssl_opts) != static_cast<long>(ssl_options::none)
#else
            ssl_opts != ssl_options::none
#endif
        )
        {
            set_ssl_options(ssl_opts);
        }

        request_handler_.set_method(RequestHandler::method::POST);
        request_handler_.set_content_type("text/xml; charset=utf-8");
        set_credentials(creds);
    }

    //! \brief Sets the schema version that will be used in requests made
    //! by this service
    void set_request_server_version(server_version vers)
    {
        server_version_ = internal::enum_to_str(vers);
    }

    //! \brief Sets the time zone ID used in the header of the request
    //! made by this service
    void set_time_zone(const time_zone time_zone) { time_zone_ = time_zone; }

    //! \brief Returns the time zone ID currently used for the header
    //! of the request made by this service
    time_zone get_time_zone() { return time_zone_; }

    //! \brief Sets maximum time the request is allowed to take.
    //!
    //! This has been tested and works for short timeout values ( \c <2),
    //! longer periods seem not to work.
    //!
    //! To remove any hard limit on a network communication (the default),
    //! set the timeout to \c 0.
    void set_timeout(std::chrono::seconds d)
    {
        request_handler_.set_timeout(d);
    }

    //! \brief Sets the URL of the proxy which should be used for connecting
    //! to the EWS service
    void set_proxy(const std::string& url)
    {
        request_handler_.set_option(CURLOPT_PROXY, url.c_str());
    }

    //! \brief Activate or deactivates HTTP proxy tunneling.
    void set_http_proxy_tunnel(const bool value)
    {
        request_handler_.set_option(CURLOPT_HTTPPROXYTUNNEL, (value ? 1L : 0L));
    }

#if LIBCURL_VERSION_NUM >= 0x073400
    //! Sets the path to proxy Certificate Authority (CA) bundle.
    //! Only available, if libcurl 7.52 or later is used.
    void set_proxy_cainfo(const std::string& path)
    {
        request_handler_.set_option(CURLOPT_PROXY_CAINFO, path.c_str());
    }

    //! Sets the path to the directory holding proxy CA certificates.
    //! Only available, if libcurl 7.52 or later is used.
    void set_proxy_capath(const std::string& path)
    {
        request_handler_.set_option(CURLOPT_PROXY_CAPATH, path.c_str());
    }
#endif

    //! \brief Sets the path to file holding certificates.
    void set_cainfo(const std::string& path)
    {
        request_handler_.set_option(CURLOPT_CAINFO, path.c_str());
    }

    //! \brief Sets the path to the certificates directory.
    void set_capath(const std::string& path)
    {
        request_handler_.set_option(CURLOPT_CAPATH, path.c_str());
    }

    //! \brief Sets the SSL options.
    void set_ssl_options(const ssl_options value)
    {
        request_handler_.set_option(CURLOPT_SSL_OPTIONS, value);
    }

    //! \brief Sets the callback for debug messages.
    void set_debug_callback(const debug_callback& callback)
    {
        debug_callback_ = callback;
        if (callback)
        {
            request_handler_.set_option(CURLOPT_VERBOSE, 1L);
            request_handler_.set_option(CURLOPT_DEBUGFUNCTION,
                                        curl_debug_callback);
            request_handler_.set_option(CURLOPT_DEBUGDATA, &debug_callback_);
        }
        else
        {
            request_handler_.set_option(CURLOPT_VERBOSE, 0L);
            request_handler_.set_option(CURLOPT_DEBUGFUNCTION, nullptr);
            request_handler_.set_option(CURLOPT_DEBUGDATA, nullptr);
        }
    }

    void set_expect(const std::string& value)
    {
        request_handler_.set_expect(value);
    }

    void set_credentials(const internal::credentials& creds)
    {
        request_handler_.set_credentials(creds);
        // If BASIC auth is used, the expect header has not
        // be sent. Otherwise the Exchange will respond with
        // 417 Expectation failed.
        if (dynamic_cast<const basic_credentials*>(&creds))
        {
            set_expect("");
        }
    }

    void set_server_uri(const std::string& value)
    {
        request_handler_.set_option(CURLOPT_URL, value.c_str());
    }

private:
    //! \brief CURL callback for forwarding debug output
    static int curl_debug_callback(EWS_MAYBE_UNUSED CURL* handle,
                                   curl_infotype type, char* data, size_t size,
                                   void* user_ptr)
    {
        std::stringstream output;

        switch (type)
        {
        case CURLINFO_TEXT:
            output << "== Info: ";
            output.write(data, size);
            break;
        case CURLINFO_HEADER_OUT:
            output << "=> Send header ";
            output.write(data, size);
            break;
        case CURLINFO_DATA_OUT:
            output << "=> Send data" << std::endl;
            break;
        case CURLINFO_SSL_DATA_OUT:
            output << "=> Send SSL data" << std::endl;
            break;
        case CURLINFO_HEADER_IN:
            output << "<= Recv header ";
            output.write(data, size);
            break;
        case CURLINFO_DATA_IN:
            output << "<= Recv data" << std::endl;
            break;
        case CURLINFO_SSL_DATA_IN:
            output << "<= Recv SSL data" << std::endl;
            break;
        default:
            output << "-- Unknown info type" << std::endl;
            break;
        }

        if (user_ptr)
        {
            (*reinterpret_cast<debug_callback*>(user_ptr))(output.str());
        }

        return 0;
    }

public:
    //! \brief Returns the schema version that is used in requests by this
    //! service
    server_version get_request_server_version() const
    {
        return internal::str_to_server_version(server_version_);
    }

    basic_service& impersonate()
    {
        impersonation_.clear();
        return *this;
    }

    basic_service& impersonate(const connecting_sid& sid)
    {
        impersonation_ = sid.to_xml();
        return *this;
    }

    //! Gets all room lists in the Exchange store.
    std::vector<mailbox> get_room_lists()
    {
        std::string msg = "<m:GetRoomLists />";
        auto response = request(msg);
        const auto response_message =
            internal::get_room_lists_response_message::parse(
                std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        return response_message.items();
    }

    //! Gets all rooms from a room list in the Exchange store.
    std::vector<mailbox> get_rooms(const mailbox& room_list)
    {
        std::stringstream sstr;
        sstr << "<m:GetRooms>"
                "<m:RoomList>"
                "<t:EmailAddress>"
             << room_list.value()
             << "</t:EmailAddress>"
                "</m:RoomList>"
                "</m:GetRooms>";

        auto response = request(sstr.str());
        const auto response_message =
            internal::get_rooms_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        return response_message.items();
    }

    //! Synchronizes the folder hierarchy in the Exchange store.
    sync_folder_hierarchy_result
    sync_folder_hierarchy(const folder_id& folder_id)
    {
        return sync_folder_hierarchy(folder_id, "");
    }

    sync_folder_hierarchy_result
    sync_folder_hierarchy(const folder_id& folder_id,
                          const std::string& sync_state)
    {
        return sync_folder_hierarchy_impl(folder_id, sync_state);
    }

    //! Synchronizes a folder in the Exchange store.
    sync_folder_items_result sync_folder_items(const folder_id& folder_id,
                                               int max_changes_returned = 512)
    {
        return sync_folder_items(folder_id, "", max_changes_returned);
    }

    sync_folder_items_result sync_folder_items(const folder_id& folder_id,
                                               const std::string& sync_state,
                                               int max_changes_returned = 512)
    {
        std::vector<item_id> ignored_items;
        return sync_folder_items(folder_id, sync_state, ignored_items,
                                 max_changes_returned);
    }

    sync_folder_items_result
    sync_folder_items(const folder_id& folder_id, const std::string& sync_state,
                      const std::vector<item_id>& ignored_items,
                      int max_changes_returned = 512)
    {
        return sync_folder_items_impl(folder_id, sync_state, ignored_items,
                                      max_changes_returned);
    }

    //! Gets a folder from the Exchange store.
    folder get_folder(const folder_id& id)
    {
        return get_folder_impl(id, base_shape::all_properties);
    }

    //! Gets a folder from the Exchange store.
    folder get_folder(const folder_id& id,
                      const std::vector<property_path>& additional_properties)
    {
        return get_folder_impl(id, base_shape::all_properties,
                               additional_properties);
    }

    //! Gets a list of folders from the Exchange store.
    std::vector<folder> get_folders(const std::vector<folder_id>& ids)
    {
        return get_folders_impl(ids, base_shape::all_properties);
    }

    //! Gets a list of folders from Exchange store.
    std::vector<folder>
    get_folders(const std::vector<folder_id>& ids,
                const std::vector<property_path>& additional_properties)
    {
        return get_folders(ids, base_shape::all_properties,
                           additional_properties);
    }

    //! Gets a task from the Exchange store.
    task get_task(const item_id& id, const item_shape& shape = item_shape())
    {
        return get_item_impl<task>(id, shape);
    }

    //! Gets multiple tasks from the Exchange store.
    std::vector<task> get_tasks(const std::vector<item_id>& ids,
                                const item_shape& shape = item_shape())
    {
        return get_item_impl<task>(ids, shape);
    }

    //! Gets a contact from the Exchange store.
    contact get_contact(const item_id& id,
                        const item_shape& shape = item_shape())
    {
        return get_item_impl<contact>(id, shape);
    }

    //! Gets multiple contacts from the Exchange store.
    std::vector<contact> get_contacts(const std::vector<item_id>& ids,
                                      const item_shape& shape = item_shape())
    {
        return get_item_impl<contact>(ids, shape);
    }

    //! Gets a calendar item from the Exchange store.
    calendar_item get_calendar_item(const item_id& id,
                                    const item_shape& shape = item_shape())
    {
        return get_item_impl<calendar_item>(id, shape);
    }

    //! Gets a bunch of calendar items from the Exchange store at once.
    std::vector<calendar_item>
    get_calendar_items(const std::vector<item_id>& ids,
                       const item_shape& shape = item_shape())
    {
        return get_item_impl<calendar_item>(ids, shape);
    }

    //! Gets a calendar item from the Exchange store.
    calendar_item get_calendar_item(const occurrence_item_id& id,
                                    const item_shape& shape = item_shape())
    {
        const std::string request_string = "<m:GetItem>" + shape.to_xml() +
                                           "<m:ItemIds>" + id.to_xml() +
                                           "</m:ItemIds>"
                                           "</m:GetItem>";

        auto response = request(request_string);
        const auto response_message =
            internal::get_item_response_message<calendar_item>::parse(
                std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        check(!response_message.items().empty(), "Expected at least one item");
        return response_message.items().front();
    }

    //! Gets a bunch of calendar items from the Exchange store at once.
    std::vector<calendar_item>
    get_calendar_items(const std::vector<occurrence_item_id>& ids,
                       const item_shape& shape = item_shape())
    {
        check(!ids.empty(), "Expected at least one item in given vector");

        std::stringstream sstr;
        sstr << "<m:GetItem>";
        sstr << shape.to_xml();
        sstr << "<m:ItemIds>";
        for (const auto& id : ids)
        {
            sstr << id.to_xml();
        }
        sstr << "</m:ItemIds>"
                "</m:GetItem>";

        auto response = request(sstr.str());
        const auto response_messages =
            internal::item_response_messages<calendar_item>::parse(
                std::move(response));
        if (!response_messages.success())
        {
            throw exchange_error(response_messages.first_error_or_warning());
        }
        return response_messages.items();
    }

    //! Gets a message item from the Exchange store.
    message get_message(const item_id& id,
                        const item_shape& shape = item_shape())
    {
        return get_item_impl<message>(id, shape);
    }

    //! Gets multiple message items from the Exchange store.
    std::vector<message> get_messages(const std::vector<item_id>& ids,
                                      const item_shape& shape = item_shape())
    {
        return get_item_impl<message>(ids, shape);
    }

    //! Delete a folder from the Exchange store
    void delete_folder(const folder_id& id,
                       delete_type del_type = delete_type::hard_delete)
    {
        const std::string request_string = "<m:DeleteFolder "
                                           "DeleteType=\"" +
                                           internal::enum_to_str(del_type) +
                                           "\">"
                                           "<m:FolderIds>" +
                                           id.to_xml() +
                                           "</m:FolderIds>"
                                           "</m:DeleteFolder>";

        auto response = request(request_string);
        const auto response_message =
            internal::delete_folder_response_message::parse(
                std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
    }

    //! Delete an arbitrary item from the Exchange store
    void delete_item(const item_id& id,
                     delete_type del_type = delete_type::hard_delete,
                     affected_task_occurrences affected =
                         affected_task_occurrences::all_occurrences,
                     send_meeting_cancellations cancellations =
                         send_meeting_cancellations::send_to_none)
    {
        using internal::delete_item_response_message;

        const std::string request_string =
            "<m:DeleteItem "
            "DeleteType=\"" +
            internal::enum_to_str(del_type) +
            "\" "
            "SendMeetingCancellations=\"" +
            internal::enum_to_str(cancellations) +
            "\" "
            "AffectedTaskOccurrences=\"" +
            internal::enum_to_str(affected) +
            "\">"
            "<m:ItemIds>" +
            id.to_xml() +
            "</m:ItemIds>"
            "</m:DeleteItem>";

        auto response = request(request_string);
        const auto response_message =
            delete_item_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
    }

    //! Delete a task item from the Exchange store
    void delete_task(task&& the_task,
                     delete_type del_type = delete_type::hard_delete,
                     affected_task_occurrences affected =
                         affected_task_occurrences::all_occurrences)
    {
        delete_item(the_task.get_item_id(), del_type, affected);
        the_task = task();
    }

    //! Delete a contact from the Exchange store
    void delete_contact(contact&& the_contact)
    {
        delete_item(the_contact.get_item_id());
        the_contact = contact();
    }

    //! Delete a calendar item from the Exchange store
    void delete_calendar_item(calendar_item&& the_calendar_item,
                              delete_type del_type = delete_type::hard_delete,
                              send_meeting_cancellations cancellations =
                                  send_meeting_cancellations::send_to_none)
    {
        delete_item(the_calendar_item.get_item_id(), del_type,
                    affected_task_occurrences::all_occurrences, cancellations);
        the_calendar_item = calendar_item();
    }

    //! Delete a message item from the Exchange store
    void delete_message(message&& the_message)
    {
        delete_item(the_message.get_item_id());
        the_message = message();
    }

    // Following items can be created in Exchange:
    //
    // - Calendar items
    // - E-mail messages
    // - Meeting requests
    // - Tasks
    // - Contacts
    //
    // Only purpose of this overload-set is to prevent exploding template
    // code in errors messages in caller's code.

    //! \brief Create a new folder in the Exchange store.
    //!
    //! \param new_folder The new folder that specified the display name.
    //! \param parent_folder The parent folder of the new folder.
    //!
    //! \return The new folders folder_id if successful.
    folder_id create_folder(const folder& new_folder,
                            const folder_id& parent_folder)
    {
        return create_folder_impl(new_folder, parent_folder);
    }

    //! \brief Create a new folder in the Exchange store.
    //!
    //! \param new_folders The new folder that specified the display name.
    //! \param parent_folder The parent folder of the new folder.
    //!
    //! \return The new folders folder_id if successful.
    std::vector<folder_id> create_folder(const std::vector<folder>& new_folders,
                                         const folder_id& parent_folder)
    {
        return create_folder_impl(new_folders, parent_folder);
    }

    //! \brief Creates a new task from the given object in the Exchange store.
    //!
    //! \return The new task's item_id if successful.
    item_id create_item(const task& the_task)
    {
        return create_item_impl(the_task, folder_id());
    }

    //! \brief Creates a new task from the given object in the the specified
    //! folder.
    //!
    //! \param the_task The task that is about to be created.
    //! \param folder The target folder where the task is saved.
    //!
    //! \return The new task's item_id if successful.
    item_id create_item(const task& the_task, const folder_id& folder)
    {
        return create_item_impl(the_task, folder);
    }

    //! \brief Creates new tasks from the given vector in the Exchange store.
    //!
    //! \return A vector of item_ids if successful.
    std::vector<item_id> create_item(const std::vector<task>& tasks)
    {
        return create_item_impl(tasks, folder_id());
    }

    //! \brief Creates new tasks from the given vector in the the specified
    //! folder.
    //!
    //! \param tasks The tasks that are about to be created.
    //! \param folder The target folder where the tasks are saved.
    //!
    //! \return A vector of item_ids if successful.
    std::vector<item_id> create_item(const std::vector<task>& tasks,
                                     const folder_id& folder)
    {
        return create_item_impl(tasks, folder);
    }

    //! \brief Creates a new contact from the given object in the Exchange
    //! store.
    //!
    //! \return The new contact's item_id if successful.
    item_id create_item(const contact& the_contact)
    {
        return create_item_impl(the_contact, folder_id());
    }

    //! \brief Creates a new contact from the given object in the specified
    //! folder.
    //!
    //! \param the_contact The contact that is about to be created.
    //! \param folder The target folder where the contact is saved.
    //!
    //! \return The new contact's item_id if successful.
    item_id create_item(const contact& the_contact, const folder_id& folder)
    {
        return create_item_impl(the_contact, folder);
    }

    //! \brief Creates new contacts from the given vector in the Exchange
    //! store.
    //!
    //! \return A vector of item_ids if successful.
    std::vector<item_id> create_item(const std::vector<contact>& contacts)
    {
        return create_item_impl(contacts, folder_id());
    }

    //! \brief Creates new contacts from the given vector in the specified
    //! folder.
    //!
    //! \param contacts The contacts that are about to be created.
    //! \param folder The target folder where the contact is saved.
    //!
    //! \return A vector of item_ids if successful.
    std::vector<item_id> create_item(const std::vector<contact>& contacts,
                                     const folder_id& folder)
    {
        return create_item_impl(contacts, folder);
    }

    //! \brief Creates a new calendar item from the given object in the Exchange
    //! store.
    //!
    //! \param the_calendar_item The calendar item that is about to be created.
    //! \param send_invitations Whether to send invitations to any participants.
    //!
    //! \return The new calendar items's item_id if successful.
    item_id create_item(const calendar_item& the_calendar_item,
                        send_meeting_invitations send_invitations =
                            send_meeting_invitations::send_to_none)
    {
        return create_item_impl(the_calendar_item, send_invitations,
                                folder_id());
    }

    //! \brief Creates a new calendar item from the given object in the
    //! specified folder.
    //!
    //! \param the_calendar_item The calendar item that is about to be created.
    //! \param send_invitations Whether to send invitations to any participants.
    //! \param folder The target folder where the calendar item is saved.
    //!
    //! \return The new calendar items's item_id if successful.
    item_id create_item(const calendar_item& the_calendar_item,
                        send_meeting_invitations send_invitations,
                        const folder_id& folder)
    {
        return create_item_impl(the_calendar_item, send_invitations, folder);
    }

    //! \brief Creates new calendar items from the given vector in the Exchange
    //! store.
    //!
    //! \param calendar_items The calendar items that are about to be created.
    //! \param send_invitations Whether to send invitations to any participants.
    //!
    //! \return A vector of item_ids if successful.
    std::vector<item_id>
    create_item(const std::vector<calendar_item>& calendar_items,
                send_meeting_invitations send_invitations =
                    send_meeting_invitations::send_to_none)
    {
        return create_item_impl(calendar_items, send_invitations, folder_id());
    }

    //! \brief Creates new calendar items from the given vector in the
    //! specified folder.
    //!
    //! \param calendar_items The calendar items that are about to be created.
    //! \param send_invitations Whether to send invitations to any participants.
    //! \param folder The target folder where the calendar item is saved.
    //!
    //! \return A vector of item_ids if successful.
    std::vector<item_id>
    create_item(const std::vector<calendar_item>& calendar_items,
                send_meeting_invitations send_invitations,
                const folder_id& folder)
    {
        return create_item_impl(calendar_items, send_invitations, folder);
    }

    //! \brief Creates a new message in the Exchange store.
    //!
    //! Creates a new message and, depending of the chosen message
    //! disposition, sends it to the recipients.
    //!
    //! Note that if you pass message_disposition::send_only or
    //! message_disposition::send_and_save_copy this function always
    //! returns an invalid item id because Exchange does not include
    //! the item identifier in the response. A common workaround for this
    //! would be to create the item with message_disposition::save_only, get
    //! the item identifier, and then use the send_item to send the message.
    //!
    //! \return The item id of the saved message when
    //! message_disposition::save_only was given; otherwise an invalid item
    //! id.
    item_id create_item(const message& the_message,
                        message_disposition disposition)
    {
        return create_item_impl(the_message, disposition, folder_id());
    }

    //! \brief Creates a new message in the specified folder.
    //!
    //! \param the_message The message item that is about to be created.
    //! \param disposition Whether the message is only saved, only send, or
    //! saved and send.
    //! \param folder The target folder where the message is saved.
    //!
    //! \return The item id of the saved message when
    //! message_disposition::save_only was given; otherwise an invalid item
    //! id.
    item_id create_item(const message& the_message,
                        message_disposition disposition,
                        const folder_id& folder)
    {
        return create_item_impl(the_message, disposition, folder);
    }

    //! \brief Creates new messages in the Exchange store.
    //!
    //! Creates new messages and, depending of the chosen message
    //! disposition, sends them to the recipients.
    //!
    //! Note that if you pass message_disposition::send_only or
    //! message_disposition::send_and_save_copy this function always
    //! returns an invalid item id because Exchange does not include
    //! the item identifier in the response. A common workaround for this
    //! would be to create the item with message_disposition::save_only, get
    //! the item identifier, and then use the send_item to send the message.
    //!
    //! \return A vector of the item ids of the saved messages when
    //! message_disposition::save_only was given; otherwise a vector of invalid
    //! item ids.
    std::vector<item_id> create_item(const std::vector<message>& messages,
                                     message_disposition disposition)
    {
        return create_item_impl(messages, disposition, folder_id());
    }

    //! \brief Creates new messages in the specified folder.
    //!
    //! \param messages The message items that are about to be created.
    //! \param disposition Whether the message is only saved, only send, or
    //! saved and send.
    //! \param folder The target folder where the message is saved.
    //!
    //! \return A vector of the item ids of the saved messages when
    //! message_disposition::save_only was given; otherwise a vector of invalid
    //! item ids.
    std::vector<item_id> create_item(const std::vector<message>& messages,
                                     message_disposition disposition,
                                     const folder_id& folder)
    {
        return create_item_impl(messages, disposition, folder);
    }

    //! Sends a message that is already in the Exchange store.
    //!
    //! \param id The item id of the message you want to send
    void send_item(const item_id& id) { send_item(id, folder_id()); }

    //! Sends messages that are already in the Exchange store.
    //!
    //! \param ids The item ids of the messages you want to send.
    void send_item(const std::vector<item_id>& ids)
    {
        send_item(ids, folder_id());
    }

    //! \brief Sends a message that is already in the Exchange store.
    //!
    //! \param id The item id of the message you want to send
    //! \param folder The folder in the mailbox in which the send message is
    //! saved. If you pass an invalid id here, the message won't be saved.
    void send_item(const item_id& id, const folder_id& folder)
    {
        std::stringstream sstr;
        sstr << "<m:SendItem SaveItemToFolder=\"" << std::boolalpha
             << folder.valid() << "\">"
             << "<m:ItemIds>" << id.to_xml() << "</m:ItemIds>";
        if (folder.valid())
        {
            sstr << "<m:SavedItemFolderId>" << folder.to_xml()
                 << "</m:SavedItemFolderId>";
        }
        sstr << "</m:SendItem>";

        auto response = request(sstr.str());

        const auto response_message =
            internal::send_item_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
    }

    //! \brief Sends messages that are already in the Exchange store.
    //!
    //! \param ids The item ids of the messages you want to send
    //! \param folder The folder in the mailbox in which the send messages are
    //! saved. If you pass an invalid id here, the messages won't be saved.
    void send_item(const std::vector<item_id>& ids, const folder_id& folder)
    {
        std::stringstream sstr;
        sstr << "<m:SendItem SaveItemToFolder=\"" << std::boolalpha
             << folder.valid() << "\">";
        for (const auto& id : ids)
        {
            sstr << "<m:ItemIds>" << id.to_xml() << "</m:ItemIds>";
        }
        if (folder.valid())
        {
            sstr << "<m:SavedItemFolderId>" << folder.to_xml()
                 << "</m:SavedItemFolderId>";
        }
        sstr << "</m:SendItem>";

        auto response = request(sstr.str());

        const auto response_message =
            internal::send_item_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
    }

    //! \brief Sends a \<FindFolder/> operation to the server
    //!
    //! Returns all subfolder in a specified folder from the users
    //! mailbox.
    //!
    //! \param parent_folder_id The parent folder in the mailbox
    //!
    //! Returns a list of subfolder (item_ids) that are located inside
    //! the specified parent folder
    std::vector<folder_id> find_folder(const folder_id& parent_folder_id)
    {
        const std::string request_string =
            "<m:FindFolder Traversal=\"Shallow\">"
            "<m:FolderShape>"
            "<t:BaseShape>IdOnly</t:BaseShape>"
            "</m:FolderShape>"
            "<m:ParentFolderIds>" +
            parent_folder_id.to_xml() +
            "</m:ParentFolderIds>"
            "</m:FindFolder>";

        auto response = request(request_string);
        const auto response_message =
            internal::find_folder_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        return response_message.items();
    }

    std::vector<item_id> find_item(const folder_id& parent_folder_id,
                                   const paging_view& view)
    {
        const std::string request_string =
            "<m:FindItem Traversal=\"Shallow\">"
            "<m:ItemShape>"
            "<t:BaseShape>IdOnly</t:BaseShape>"
            "</m:ItemShape>" +
            view.to_xml() + "<m:ParentFolderIds>" + parent_folder_id.to_xml() +
            "</m:ParentFolderIds>"
            "</m:FindItem>";

        auto response = request(request_string);
        const auto response_message =
            internal::find_item_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        return response_message.items();
    }

    std::vector<item_id> find_item(const folder_id& parent_folder_id)
    {
        // TODO: add item_shape to function parameters
        const std::string request_string = "<m:FindItem Traversal=\"Shallow\">"
                                           "<m:ItemShape>"
                                           "<t:BaseShape>IdOnly</t:BaseShape>"
                                           "</m:ItemShape>"
                                           "<m:ParentFolderIds>" +
                                           parent_folder_id.to_xml() +
                                           "</m:ParentFolderIds>"
                                           "</m:FindItem>";

        auto response = request(request_string);
        const auto response_message =
            internal::find_item_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        return response_message.items();
    }

    //! \brief Returns all calendar items in given calendar view.
    //!
    //! Sends a \<FindItem/> operation to the server containing a
    //! \<CalendarView/> element. It returns single calendar items and all
    //! occurrences of recurring meetings.
    std::vector<calendar_item> find_item(const calendar_view& view,
                                         const folder_id& parent_folder_id,
                                         const item_shape& shape = item_shape())
    {
        const std::string request_string =
            "<m:FindItem Traversal=\"Shallow\">" + shape.to_xml() +
            view.to_xml() + "<m:ParentFolderIds>" + parent_folder_id.to_xml() +
            "</m:ParentFolderIds>"
            "</m:FindItem>";

        auto response = request(request_string);
        const auto response_message =
            internal::find_calendar_item_response_message::parse(
                std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        return response_message.items();
    }

    //! \brief Sends a \<FindItem/> operation to the server
    //!
    //! Allows you to search for items that are located in a user's
    //! mailbox.
    //!
    //! \param parent_folder_id The folder in the mailbox that is searched
    //! \param restriction A search expression that restricts the elements
    //! returned by this operation
    //!
    //! Returns a list of items (item_ids) that match given folder and
    //! restrictions
    std::vector<item_id> find_item(const folder_id& parent_folder_id,
                                   search_expression restriction)
    {
        // TODO: add item_shape to function parameters
        const std::string request_string = "<m:FindItem Traversal=\"Shallow\">"
                                           "<m:ItemShape>"
                                           "<t:BaseShape>IdOnly</t:BaseShape>"
                                           "</m:ItemShape>"
                                           "<m:Restriction>" +
                                           restriction.to_xml() +
                                           "</m:Restriction>"
                                           "<m:ParentFolderIds>" +
                                           parent_folder_id.to_xml() +
                                           "</m:ParentFolderIds>"
                                           "</m:FindItem>";

        auto response = request(request_string);
        const auto response_message =
            internal::find_item_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        return response_message.items();
    }

    //! \brief Update an existing item's property.
    //!
    //! Sends an \<UpdateItem> request to the server. Allows you to change
    //! properties of existing items in the Exchange store.
    //!
    //! \param id The id of the item you want to change.
    //! \param change The update to the item.
    //! \param resolution The conflict resolution mode during the update;
    //! normally AutoResolve.
    //! \param invitations_or_cancellations Specifies how meeting updates are
    //! communicated to other participants. Only meaningful (and mandatory) if
    //! the item is a calendar item.
    //!
    //! \return The updated item's new id and change_key upon success.
    item_id update_item(
        item_id id, update change,
        conflict_resolution resolution = conflict_resolution::auto_resolve,
        send_meeting_invitations_or_cancellations invitations_or_cancellations =
            send_meeting_invitations_or_cancellations::send_to_none)
    {
        return update_item_impl(std::move(id), std::move(change), resolution,
                                invitations_or_cancellations, folder_id());
    }

    //! \brief Update an existing item's property in the specified folder.
    //!
    //! Sends an \<UpdateItem> request to the server. Allows you to change
    //! properties of an existing item that is located in
    //! the specified folder.
    //!
    //! \param id The id of the item you want to change.
    //! \param change The update to the item.
    //! \param resolution The conflict resolution mode during the update;
    //! normally AutoResolve.
    //! \param invitations_or_cancellations Specifies how meeting updates are
    //! communicated to other participants. Only meaningful (and mandatory) if
    //! the item is a calendar item. \param folder Specified the target folder
    //! for this operation. This is useful if you want to gain implicit delegate
    //! access to another user's items.
    //!
    //! \return The updated item's new id and change_key upon success.
    item_id update_item(
        item_id id, update change, conflict_resolution resolution,
        send_meeting_invitations_or_cancellations invitations_or_cancellations,
        const folder_id& folder)
    {
        return update_item_impl(std::move(id), std::move(change), resolution,
                                invitations_or_cancellations, folder);
    }

    //! \brief Update multiple properties of an existing item.
    //!
    //! Sends an \<UpdateItem> request to the server. Allows you to change
    //! multiple properties at once.
    //!
    //! \param id The id of the item you want to change.
    //! \param changes A list of updates to the item.
    //! \param resolution The conflict resolution mode during the update;
    //! normally AutoResolve.
    //! \param invitations_or_cancellations Specifies how meeting updates are
    //! communicated to other participants. Only meaningful if the item is a
    //! calendar item.
    //!
    //! \return The updated item's new id and change_key upon success.
    item_id update_item(
        item_id id, const std::vector<update>& changes,
        conflict_resolution resolution = conflict_resolution::auto_resolve,
        send_meeting_invitations_or_cancellations invitations_or_cancellations =
            send_meeting_invitations_or_cancellations::send_to_none)
    {
        return update_item_impl(std::move(id), changes, resolution,
                                invitations_or_cancellations, folder_id());
    }

    //! \brief Update multiple properties of an existing item in the specified
    //! folder.
    //!
    //! Sends an \<UpdateItem> request to the server. Allows you to change
    //! multiple properties at once.
    //!
    //! \param id The id of the item you want to change.
    //! \param changes A list of updates to the item.
    //! \param resolution The conflict resolution mode during the update;
    //! normally AutoResolve.
    //! \param invitations_or_cancellations Specifies how meeting updates are
    //! communicated to other participants. Only meaningful if the item is a
    //! calendar item. \param folder Specified the target folder for this
    //! operation. This is useful if you want to gain implicit delegate access
    //! to another user's items.
    //!
    //! \return The updated item's new id and change_key upon success.
    item_id update_item(
        item_id id, const std::vector<update>& changes,
        conflict_resolution resolution,
        send_meeting_invitations_or_cancellations invitations_or_cancellations,
        const folder_id& folder)
    {
        return update_item_impl(std::move(id), changes, resolution,
                                invitations_or_cancellations, folder);
    }

    //! \brief Update an existing folder's property.
    //!
    //! Sends an \<UpdateFolder> request to the server. Allows you to change
    //! properties of existing items in the Exchange store.
    //!
    //! \param folder_id The id of the folder you want to change.
    //! \param change The update to the folder.
    //!
    //! \return The updated folder's new id and change_key upon success.
    folder_id update_folder(folder_id folder_id, update change)
    {
        return update_folder_impl(std::move(folder_id), std::move(change));
    }

    //! \brief Update multiple properties of an existing folder.
    //!
    //! Sends an \<UpdateFolder> request to the server. Allows you to change
    //! multiple properties at once.
    //!
    //! \param folder_id The id of the folder you want to change.
    //! \param changes A list of updates to the folder.
    //!
    //! \return The updated folder's new id and change_key upon success.
    folder_id update_folder(folder_id folder_id,
                            const std::vector<update>& changes)
    {
        return update_folder_impl(std::move(folder_id), changes);
    }

    //! \brief Moves one item to a folder.
    //!
    //! \param item The id of the item you want to move.
    //! \param folder The id of the target folder.
    //!
    //! \return The new id of the item that has been moved.
    item_id move_item(item_id item, const folder_id& folder)
    {
        return move_item_impl(std::move(item), folder);
    }

    //! \brief Moves one or more items to a folder.
    //!
    //! \param items A list of ids of items that shall be moved.
    //! \param folder The id of the target folder.
    //!
    //! \return A vector of new ids of the items that have been moved.
    std::vector<item_id> move_item(const std::vector<item_id>& items,
                                   const folder_id& folder)
    {
        return move_item_impl(items, folder);
    }

    //! \brief Moves one folder to a folder.
    //!
    //! \param folder The id of the folder you want to move.
    //! \param target The id of the target folder.
    //!
    //! \return The new item_id of the folder that has been moved.
    folder_id move_folder(folder_id folder, const folder_id& target)
    {
        return move_folder_impl(std::move(folder), target);
    }

    //! \brief Moves one or more folders to a target folder.
    //!
    //! \param folders A list of ids of folders that shall be moved.
    //! \param target The id of the target folder.
    //!
    //! \return A vector of new ids of the folders that have been moved.
    std::vector<folder_id> move_folder(const std::vector<folder_id>& folders,
                                       const folder_id& target)
    {
        return move_folder_impl(folders, target);
    }

    //! \brief Add new delegates to given mailbox
    std::vector<delegate_user>
    add_delegate(const mailbox& mailbox,
                 const std::vector<delegate_user>& delegates)
    {
        std::stringstream sstr;
        sstr << "<m:AddDelegate>";
        sstr << mailbox.to_xml("m");
        sstr << "<m:DelegateUsers>";
        for (const auto& delegate : delegates)
        {
            sstr << delegate.to_xml();
        }
        sstr << "</m:DelegateUsers>";
        sstr << "</m:AddDelegate>";
        auto response = request(sstr.str());

        const auto response_message =
            internal::add_delegate_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        return response_message.get_delegates();
    }

    //! \brief Retrieves the delegate users and settings for the specified
    //! mailbox.
    std::vector<delegate_user> get_delegate(const mailbox& mailbox,
                                            bool include_permissions = false)
    {
        std::stringstream sstr;
        sstr << "<m:GetDelegate IncludePermissions=\""
             << (include_permissions ? "true" : "false") << "\">";
        sstr << mailbox.to_xml("m");
        sstr << "</m:GetDelegate>";
        auto response = request(sstr.str());

        const auto response_message =
            internal::get_delegate_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        return response_message.get_delegates();
    }

    void remove_delegate(const mailbox& mailbox,
                         const std::vector<user_id>& delegates)
    {
        std::stringstream sstr;
        sstr << "<m:RemoveDelegate>";
        sstr << mailbox.to_xml("m");
        sstr << "<m:UserIds>";
        for (const auto& user : delegates)
        {
            sstr << user.to_xml();
        }
        sstr << "</m:UserIds>";
        sstr << "</m:RemoveDelegate>";
        auto response = request(sstr.str());

        const auto response_message =
            internal::remove_delegate_response_message::parse(
                std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
    }

    //! \brief Lets you attach a file (or another item) to an existing item.
    //!
    //! \param parent_item An existing item in the Exchange store
    //! \param a The <tt>\<FileAttachment></tt> or
    //!        <tt>\<ItemAttachment></tt> you want to attach to
    //!        \p parent_item
    attachment_id create_attachment(const item_id& parent_item,
                                    const attachment& a)
    {
        auto response = request("<m:CreateAttachment>"
                                "<m:ParentItemId Id=\"" +
                                parent_item.id() + "\" ChangeKey=\"" +
                                parent_item.change_key() +
                                "\"/>"
                                "<m:Attachments>" +
                                a.to_xml() +
                                "</m:Attachments>"
                                "</m:CreateAttachment>");

        const auto response_message =
            internal::create_attachment_response_message::parse(
                std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        check(!response_message.attachment_ids().empty(),
              "Expected at least one attachment");
        return response_message.attachment_ids().front();
    }

#if 0
        // TODO

        //! \brief Attach one or more files (or items) to an existing item.
        //!
        //! Use this member-function when you want to create multiple
        //! attachments to a single item. If you want to create attachments for
        //! multiple items, you will need to call create_attachment several
        //! times--one for each item that you want to add attachments to.
        std::vector<attachment_id>
        create_attachments(const item& parent_item,
                           const std::vector<attachment>& attachments)
        {
            (void)parent_item;
            (void)attachments;
            return std::vector<attachment_id>();
        }
#endif // 0

    //! Retrieves an attachment from the Exchange store
    attachment get_attachment(const attachment_id& id,
                              bool include_mime_content = false)
    {
        std::stringstream sstr;
        sstr << "<m:GetAttachment>"
                "<m:AttachmentShape>"
                "<t:IncludeMimeContent>"
             << (include_mime_content ? "true" : "false")
             << "</t:IncludeMimeContent>"
                "</m:AttachmentShape>"
                "<m:AttachmentIds>"
             << id.to_xml()
             << "</m:AttachmentIds>"
                "</m:GetAttachment>";
        auto response = request(sstr.str());

        const auto response_message =
            internal::get_attachment_response_message::parse(
                std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        check(!response_message.attachments().empty(),
              "Expected at least one attachment to be returned");
        return response_message.attachments().front();
    }

    //! \brief Deletes given attachment from the Exchange store
    //!
    //! Returns the item_id of the parent item from which the attachment
    //! was removed (also known as <em>root</em> item). This item_id
    //! contains the updated change key of the parent item.
    item_id delete_attachment(const attachment_id& id)
    {
        auto response = request("<m:DeleteAttachment>"
                                "<m:AttachmentIds>" +
                                id.to_xml() +
                                "</m:AttachmentIds>"
                                "</m:DeleteAttachment>");
        const auto response_message =
            internal::delete_attachment_response_message::parse(
                std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        return response_message.get_root_item_id();
    }

    //! \brief The ResolveNames operation resolves ambiguous email addresses and
    //! display names.
    //!
    //! \param unresolved_entry Partial or full name of the user to look for
    //! \param scope The scope in which to look for the user
    //!
    //! Returns a resolution_set which contains a vector of resolutions.
    //! ContactDataShape and ReturnFullContactData are set by default. A
    //! directory_id is returned in place of the contact.
    //! If no name can be resolved, an empty resolution_set is returned.
    resolution_set resolve_names(const std::string& unresolved_entry,
                                 search_scope scope)
    {
        std::vector<folder_id> v;
        return resolve_names_impl(unresolved_entry, v, scope);
    }

    //! \brief The ResolveNames operation resolves ambiguous email addresses and
    //! display names.
    //!
    //! \param unresolved_entry Partial or full name of the user to look for
    //! \param scope The scope in which to look for the user
    //! \param parent_folder_ids Contains the folder_ids where to look
    //!
    //! Returns a resolution_set which contains a vector of resolutions.
    //! ContactDataShape and ReturnFullContactData are set by default. A
    //! directory_id is returned in place of the contact.
    //! If no name can be resolved, an empty resolution_set is returned.
    resolution_set
    resolve_names(const std::string& unresolved_entry, search_scope scope,
                  const std::vector<folder_id>& parent_folder_ids)
    {
        return resolve_names_impl(unresolved_entry, parent_folder_ids, scope);
    }

#ifdef EWS_HAS_VARIANT
    //! \brief The Subscribe operation subscribes to the specified folders and
    //! event types.
    //!
    //! \param ids Ids of the folders to subscribe to
    //! \param types The types of events to subscribe to
    //!
    //! Returns a subscription_information which contains the SubscriptionId and
    //! the Watermark
    subscription_information
    subscribe(const std::vector<distinguished_folder_id>& ids,
              const std::vector<event_type>& types, int timeout)
    {
        return subscribe_impl(ids, types, timeout);
    }

    //! \brief The Unsubscribe operation removes a subscription
    //!
    //! \param subscription_id The id of the subscription to unsubscribe from
    //!
    //! Returns void
    void unsubscribe(const std::string& subscription_id)
    {
        return unsubscribe_impl(subscription_id);
    }

    //! \brief The GetEvents operation gets all new events since the last call
    //!
    //! \param subscription_id The SubscriptionId of your subscription
    //! \param watermark
    //!
    //! Returns a notification which contains a vector of events. If no new
    //! events were created the get_events function will return a status_event.
    notification get_events(const std::string& subscription_id,
                            const std::string& watermark)
    {
        return get_events_impl(subscription_id, watermark);
    }
#endif
private:
    RequestHandler request_handler_;
    debug_callback debug_callback_;
    std::string server_version_;
    std::string impersonation_;
    time_zone time_zone_;

    // Helper for doing requests. Adds the right headers, credentials, and
    // checks the response for faults.
    internal::http_response request(const std::string& request_string)
    {
        using internal::get_element_by_qname;
        using rapidxml::internal::compare;

        auto soap_headers = std::vector<std::string>();
        soap_headers.emplace_back("<t:RequestServerVersion Version=\"" +
                                  server_version_ + "\"/>");
        if (!impersonation_.empty())
        {
            soap_headers.emplace_back("<t:ExchangeImpersonation>" +
                                      impersonation_ +
                                      "</t:ExchangeImpersonation>");
        }
        if (time_zone_ != time_zone::none)
        {
            soap_headers.emplace_back("<t:TimeZoneContext>"
                                      "<t:TimeZoneDefinition Id=\"" +
                                      internal::enum_to_str(time_zone_) +
                                      "\"/></t:TimeZoneContext>");
        }

        auto response = internal::make_raw_soap_request(
            request_handler_, request_string, soap_headers);

        if (response.ok())
        {
            return response;
        }
        else if (response.is_soap_fault())
        {
            std::unique_ptr<rapidxml::xml_document<char>> doc;

            try
            {
                doc = parse_response(std::move(response));
            }
            catch (xml_parse_error&)
            {
                throw soap_fault("The request failed for unknown reason "
                                 "(could not parse response)");
            }

            auto elem = get_element_by_qname(
                *doc, "ResponseCode", internal::uri<>::microsoft::errors());
            if (!elem)
            {
                throw soap_fault("The request failed for unknown reason "
                                 "(unexpected XML in response)");
            }

            if (compare(elem->value(), elem->value_size(),
                        "ErrorSchemaValidation", 21))
            {
                // Get some more helpful details
                elem = get_element_by_qname(
                    *doc, "LineNumber", internal::uri<>::microsoft::types());
                check(elem, "Expected <LineNumber> element in response");
                const auto line_number =
                    std::stoul(std::string(elem->value(), elem->value_size()));

                elem = get_element_by_qname(
                    *doc, "LinePosition", internal::uri<>::microsoft::types());
                check(elem, "Expected <LinePosition> element in response");
                const auto line_position =
                    std::stoul(std::string(elem->value(), elem->value_size()));

                elem = get_element_by_qname(
                    *doc, "Violation", internal::uri<>::microsoft::types());
                check(elem, "Expected <Violation> element in response");
                throw schema_validation_error(
                    line_number, line_position,
                    std::string(elem->value(), elem->value_size()));
            }
            else
            {
                elem = get_element_by_qname(*doc, "faultstring", "");
                check(elem, "Expected <faultstring> element in response");
                throw soap_fault(elem->value());
            }
        }
        else
        {
            throw http_error(response.code());
        }
    }

    sync_folder_hierarchy_result
    sync_folder_hierarchy_impl(const folder_id& folder_id,
                               const std::string& sync_state)
    {
        std::stringstream sstr;
        sstr << "<m:SyncFolderHierarchy>"
                "<m:FolderShape>"
                "<t:BaseShape>"
             << internal::enum_to_str(base_shape::default_shape)
             << "</t:BaseShape>"
                "</m:FolderShape>"
                "<m:SyncFolderId>"
             << folder_id.to_xml() << "</m:SyncFolderId>";
        if (!sync_state.empty())
        {
            sstr << "<m:SyncState>" << sync_state << "</m:SyncState>";
        }
        sstr << "</m:SyncFolderHierarchy>";

        auto response = request(sstr.str());
        const auto response_message =
            sync_folder_hierarchy_result::parse(std::move(response));
        check(!response_message.get_sync_state().empty(),
              "Expected at least a sync state");
        return response_message;
    }

    sync_folder_items_result sync_folder_items_impl(
        const folder_id& folder_id, const std::string& sync_state,
        std::vector<item_id> ignored_items, int max_changes_returned = 512)
    {
        std::stringstream sstr;
        sstr << "<m:SyncFolderItems>"
                "<m:ItemShape>"
                "<t:BaseShape>"
             << internal::enum_to_str(base_shape::id_only)
             << "</t:BaseShape>"
                "</m:ItemShape>"
                "<m:SyncFolderId>"
             << folder_id.to_xml() << "</m:SyncFolderId>";
        if (!sync_state.empty())
        {
            sstr << "<m:SyncState>" << sync_state << "</m:SyncState>";
        }
        if (!ignored_items.empty())
        {
            sstr << "<m:Ignore>";
            for (const auto& i : ignored_items)
            {
                sstr << i.to_xml();
            }
            sstr << "</m:Ignore>";
        }
        sstr << "<m:MaxChangesReturned>" << max_changes_returned
             << "</m:MaxChangesReturned>"
                "</m:SyncFolderItems>";

        auto response = request(sstr.str());
        const auto response_message =
            sync_folder_items_result::parse(std::move(response));
        check(!response_message.get_sync_state().empty(),
              "Expected at least a sync state");
        return response_message;
    }

    folder get_folder_impl(const folder_id& id, base_shape shape)
    {
        const std::string request_string = "<m:GetFolder>"
                                           "<m:FolderShape>"
                                           "<t:BaseShape>" +
                                           internal::enum_to_str(shape) +
                                           "</t:BaseShape>"
                                           "</m:FolderShape>"
                                           "<m:FolderIds>" +
                                           id.to_xml() +
                                           "</m:FolderIds>"
                                           "</m:GetFolder>";

        auto response = request(request_string);
        const auto response_message =
            internal::get_folder_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        check(!response_message.items().empty(), "Expected at least one item");
        return response_message.items().front();
    }

    folder
    get_folder_impl(const folder_id& id, base_shape shape,
                    const std::vector<property_path>& additional_properties)
    {
        check(!additional_properties.empty(),
              "Expected at least one element in additional_properties");

        std::stringstream sstr;
        sstr << "<m:GetFolder>"
                "<m:FolderShape>"
                "<t:BaseShape>"
             << internal::enum_to_str(shape)
             << "</t:BaseShape>"
                "<t:AdditionalProperties>";
        for (const auto& prop : additional_properties)
        {
            sstr << prop.to_xml();
        }
        sstr << "</t:AdditionalProperties>"
                "</m:FolderShape>"
                "<m:FolderIds>"
             << id.to_xml()
             << "</m:FolderIds>"
                "</m:GetFolder>";

        auto response = request(sstr.str());
        const auto response_message =
            internal::folder_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.first_error_or_warning());
        }
        check(!response_message.items().empty(), "Expected at least one item");
        return response_message.items().front();
    }

    std::vector<folder> get_folders_impl(const std::vector<folder_id>& ids,
                                         base_shape shape)
    {
        check(!ids.empty(), "Expected at least one element in given vector");

        std::stringstream sstr;
        sstr << "<m:GetFolder>"
                "<m:FolderShape>"
                "<t:BaseShape>"
             << internal::enum_to_str(shape)
             << "</t:BaseShape>"
                "</m:FolderShape>"
                "<m:FolderIds>";
        for (const auto& id : ids)
        {
            sstr << id.to_xml();
        }
        sstr << "</m:FolderIds>"
                "</m:GetFolder>";

        auto response = request(sstr.str());
        const auto response_messages =
            internal::folder_response_message::parse(std::move(response));
        if (!response_messages.success())
        {
            throw exchange_error(response_messages.first_error_or_warning());
        }
        return response_messages.items();
    }

    std::vector<folder>
    get_folders_impl(const std::vector<folder_id>& ids, base_shape shape,
                     const std::vector<property_path>& additional_properties)
    {
        check(!ids.empty(), "Expected at least one element in given vector");
        check(!additional_properties.empty(),
              "Expected at least one element in additional_properties");

        std::stringstream sstr;
        sstr << "<m:GetFolder>"
                "<m:FolderShape>"
                "<t:BaseShape>"
             << internal::enum_to_str(shape)
             << "</t:BaseShape>"
                "<t:AdditionalProperties>";
        for (const auto& prop : additional_properties)
        {
            sstr << prop.to_xml();
        }
        sstr << "</t:AdditionalProperties>"
                "</m:FolderShape>"
                "<m:FolderIds>";
        for (const auto& id : ids)
        {
            sstr << id.to_xml();
        }
        sstr << "</m:FolderIds>"
                "</m:GetFolder>";

        auto response = request(sstr.str());
        const auto response_messages =
            internal::folder_response_message::parse(std::move(response));
        if (!response_messages.success())
        {
            throw exchange_error(response_messages.first_error_or_warning());
        }
        return response_messages.items();
    }

    // Gets an item from the server
    template <typename ItemType>
    ItemType get_item_impl(const item_id& id, const item_shape& shape)
    {
        const std::string request_string = "<m:GetItem>" + shape.to_xml() +
                                           "<m:ItemIds>" + id.to_xml() +
                                           "</m:ItemIds>"
                                           "</m:GetItem>";

        auto response = request(request_string);
        const auto response_message =
            internal::get_item_response_message<ItemType>::parse(
                std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        check(!response_message.items().empty(), "Expected at least one item");
        return response_message.items().front();
    }

    // Gets a bunch of items from the server all at once
    template <typename ItemType>
    std::vector<ItemType> get_item_impl(const std::vector<item_id>& ids,
                                        const item_shape& shape)
    {
        check(!ids.empty(), "Expected at least one id in given vector");

        std::stringstream sstr;
        sstr << "<m:GetItem>";
        sstr << shape.to_xml();
        sstr << "<m:ItemIds>";
        for (const auto& id : ids)
        {
            sstr << id.to_xml();
        }
        sstr << "</m:ItemIds>"
                "</m:GetItem>";

        auto response = request(sstr.str());
        const auto response_messages =
            internal::item_response_messages<ItemType>::parse(
                std::move(response));
        if (!response_messages.success())
        {
            throw exchange_error(response_messages.first_error_or_warning());
        }
        return response_messages.items();
    }

    // Creates an item on the server and returns it's item_id.
    template <typename ItemType>
    item_id create_item_impl(const ItemType& the_item, const folder_id& folder)
    {
        std::stringstream sstr;
        sstr << "<m:CreateItem>";

        if (folder.valid())
        {
            sstr << "<m:SavedItemFolderId>" << folder.to_xml()
                 << "</m:SavedItemFolderId>";
        }

        sstr << "<m:Items>";
        sstr << "<t:" << the_item.item_tag_name() << ">";
        sstr << the_item.xml().to_string();
        sstr << "</t:" << the_item.item_tag_name() << ">";
        sstr << "</m:Items>"
                "</m:CreateItem>";

        auto response = request(sstr.str());
        const auto response_message =
            internal::create_item_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        check(!response_message.items().empty(), "Expected at least one item");
        return response_message.items().front();
    }

    // Creates multiple items on the server and returns the item_ids.
    template <typename ItemType>
    std::vector<item_id> create_item_impl(const std::vector<ItemType>& items,
                                          const folder_id& folder)
    {
        std::stringstream sstr;
        sstr << "<m:CreateItem>";

        if (folder.valid())
        {
            sstr << "<m:SavedItemFolderId>" << folder.to_xml()
                 << "</m:SavedItemFolderId>";
        }

        sstr << "<m:Items>";
        for (const auto& item : items)
        {
            sstr << "<t:" << item.item_tag_name() << ">";
            sstr << item.xml().to_string();
            sstr << "</t:" << item.item_tag_name() << ">";
        }
        sstr << "</m:Items>"
                "</m:CreateItem>";

        auto response = request(sstr.str());
        const auto response_messages =
            internal::item_response_messages<ItemType>::parse(
                std::move(response));
        if (!response_messages.success())
        {
            throw exchange_error(response_messages.first_error_or_warning());
        }
        check(!response_messages.items().empty(), "Expected at least one item");

        const std::vector<ItemType> res_items = response_messages.items();
        std::vector<item_id> res;
        res.reserve(res_items.size());
        std::transform(begin(res_items), end(res_items),
                       std::back_inserter(res),
                       [](const ItemType& elem) { return elem.get_item_id(); });

        return res;
    }

    item_id create_item_impl(const calendar_item& the_calendar_item,
                             send_meeting_invitations send_invitations,
                             const folder_id& folder)
    {
        using internal::create_item_response_message;

        std::stringstream sstr;
        sstr << "<m:CreateItem SendMeetingInvitations=\""
             << internal::enum_to_str(send_invitations) << "\">";

        if (folder.valid())
        {
            sstr << "<m:SavedItemFolderId>" << folder.to_xml()
                 << "</m:SavedItemFolderId>";
        }

        sstr << "<m:Items>"
                "<t:CalendarItem>";
        sstr << the_calendar_item.xml().to_string();
        sstr << "</t:CalendarItem>"
                "</m:Items>"
                "</m:CreateItem>";

        auto response = request(sstr.str());

        const auto response_message =
            create_item_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        check(!response_message.items().empty(), "Expected a message item");
        return response_message.items().front();
    }

    std::vector<item_id>
    create_item_impl(const std::vector<calendar_item>& items,
                     send_meeting_invitations send_invitations,
                     const folder_id& folder)
    {
        std::stringstream sstr;
        sstr << "<m:CreateItem SendMeetingInvitations=\""
             << internal::enum_to_str(send_invitations) << "\">";

        if (folder.valid())
        {
            sstr << "<m:SavedItemFolderId>" << folder.to_xml()
                 << "</m:SavedItemFolderId>";
        }

        sstr << "<m:Items>";
        for (const auto& item : items)
        {
            sstr << "<t:CalendarItem>";
            sstr << item.xml().to_string();
            sstr << "</t:CalendarItem>";
        }
        sstr << "</m:Items>"
                "</m:CreateItem>";

        auto response = request(sstr.str());
        const auto response_messages =
            internal::item_response_messages<calendar_item>::parse(
                std::move(response));
        if (!response_messages.success())
        {
            throw exchange_error(response_messages.first_error_or_warning());
        }
        check(!response_messages.items().empty(), "Expected at least one item");

        const std::vector<calendar_item> res_items = response_messages.items();
        std::vector<item_id> res;
        res.reserve(res_items.size());
        std::transform(
            begin(res_items), end(res_items), std::back_inserter(res),
            [](const calendar_item& elem) { return elem.get_item_id(); });

        return res;
    }

    folder_id create_folder_impl(const folder& new_folder,
                                 const folder_id& parent_folder)
    {
        check(parent_folder.valid(), "Given parent_folder is not valid");

        std::stringstream sstr;
        sstr << "<m:CreateFolder >"
                "<m:ParentFolderId>"
             << parent_folder.to_xml()
             << "</m:ParentFolderId>"
                "<m:Folders>"
                "<t:Folder>"
             << new_folder.xml().to_string()
             << "</t:Folder>"
                "</m:Folders>"
                "</m:CreateFolder>";

        auto response = request(sstr.str());
        const auto response_message =
            internal::create_folder_response_message::parse(
                std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        check(!response_message.items().empty(), "Expected at least one item");
        return response_message.items().front();
    }

    std::vector<folder_id>
    create_folder_impl(const std::vector<folder>& new_folders,
                       const folder_id& parent_folder)
    {
        check(parent_folder.valid(), "Given parent_folder is not valid");

        std::stringstream sstr;
        sstr << "<m:CreateFolder >"
                "<m:ParentFolderId>"
             << parent_folder.to_xml()
             << "</m:ParentFolderId>"
                "<m:Folders>";
        for (const auto& folder : new_folders)
        {
            sstr << "<t:Folder>" << folder.xml().to_string() << "</t:Folder>";
        }
        sstr << "</m:Folders>"
                "</m:CreateFolder>";

        auto response = request(sstr.str());
        const auto response_messages =
            internal::folder_response_message::parse(std::move(response));
        if (!response_messages.success())
        {
            throw exchange_error(response_messages.first_error_or_warning());
        }
        check(!response_messages.items().empty(), "Expected at least one item");

        const std::vector<folder> items = response_messages.items();
        std::vector<folder_id> res;
        res.reserve(items.size());
        std::transform(begin(items), end(items), std::back_inserter(res),
                       [](const folder& elem) { return elem.get_folder_id(); });

        return res;
    }

    item_id create_item_impl(const message& the_message,
                             message_disposition disposition,
                             const folder_id& folder)
    {
        std::stringstream sstr;
        sstr << "<m:CreateItem MessageDisposition=\""
             << internal::enum_to_str(disposition) << "\">";

        if (folder.valid())
        {
            sstr << "<m:SavedItemFolderId>" << folder.to_xml()
                 << "</m:SavedItemFolderId>";
        }

        sstr << "<m:Items>"
             << "<t:Message>";
        sstr << the_message.xml().to_string();
        sstr << "</t:Message>"
                "</m:Items>"
                "</m:CreateItem>";

        auto response = request(sstr.str());

        const auto response_message =
            internal::create_item_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }

        if (disposition == message_disposition::save_only)
        {
            check(!response_message.items().empty(), "Expected a message item");
            return response_message.items().front();
        }

        return item_id();
    }

    std::vector<item_id> create_item_impl(const std::vector<message>& messages,
                                          ews::message_disposition disposition,
                                          const folder_id& folder)
    {
        std::stringstream sstr;
        sstr << "<m:CreateItem MessageDisposition=\""
             << internal::enum_to_str(disposition) << "\">";

        if (folder.valid())
        {
            sstr << "<m:SavedItemFolderId>" << folder.to_xml()
                 << "</m:SavedItemFolderId>";
        }

        sstr << "<m:Items>";
        for (const auto& item : messages)
        {
            sstr << "<t:Message>";
            sstr << item.xml().to_string();
            sstr << "</t:Message>";
        }
        sstr << "</m:Items>"
                "</m:CreateItem>";

        auto response = request(sstr.str());
        const auto response_messages =
            internal::item_response_messages<message>::parse(
                std::move(response));
        if (!response_messages.success())
        {
            throw exchange_error(response_messages.first_error_or_warning());
        }
        if (disposition == message_disposition::save_only)
        {
            check(!response_messages.items().empty(),
                  "Expected at least one item");
        }

        const std::vector<message> items = response_messages.items();
        std::vector<item_id> res;
        res.reserve(items.size());
        std::transform(begin(items), end(items), std::back_inserter(res),
                       [](const message& elem) { return elem.get_item_id(); });

        return res;
    }

    item_id update_item_impl(
        item_id id, update change, conflict_resolution resolution,
        send_meeting_invitations_or_cancellations invitations_or_cancellations,
        const folder_id& folder)
    {
        std::stringstream sstr;
        sstr << "<m:UpdateItem "
                "MessageDisposition=\"SaveOnly\" "
                "ConflictResolution=\""
             << internal::enum_to_str(resolution)
             << "\" SendMeetingInvitationsOrCancellations=\""
             << internal::enum_to_str(invitations_or_cancellations) + "\">";

        if (folder.valid())
        {
            sstr << "<m:SavedItemFolderId>" << folder.to_xml()
                 << "</m:SavedItemFolderId>";
        }

        sstr << "<m:ItemChanges>"
                "<t:ItemChange>"
             << id.to_xml() << "<t:Updates>" << change.to_item_xml()
             << "</t:Updates>"
                "</t:ItemChange>"
                "</m:ItemChanges>"
                "</m:UpdateItem>";

        auto response = request(sstr.str());
        const auto response_message =
            internal::update_item_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        check(!response_message.items().empty(), "Expected at least one item");
        return response_message.items().front();
    }

    item_id
    update_item_impl(item_id id, const std::vector<update>& changes,
                     conflict_resolution resolution,
                     send_meeting_invitations_or_cancellations cancellations,
                     const folder_id& folder)
    {
        std::stringstream sstr;
        sstr << "<m:UpdateItem "
                "MessageDisposition=\"SaveOnly\" "
                "ConflictResolution=\""
             << internal::enum_to_str(resolution)
             << "\" SendMeetingInvitationsOrCancellations=\""
             << internal::enum_to_str(cancellations) + "\">";

        if (folder.valid())
        {
            sstr << "<m:SavedItemFolderId>" << folder.to_xml()
                 << "</m:SavedItemFolderId>";
        }

        sstr << "<m:ItemChanges>";
        sstr << "<t:ItemChange>" << id.to_xml() << "<t:Updates>";

        for (const auto& change : changes)
        {
            sstr << change.to_item_xml();
        }

        sstr << "</t:Updates>"
                "</t:ItemChange>"
                "</m:ItemChanges>"
                "</m:UpdateItem>";

        auto response = request(sstr.str());
        const auto response_message =
            internal::update_item_response_message::parse(std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        check(!response_message.items().empty(), "Expected at least one item");
        return response_message.items().front();
    }

    folder_id update_folder_impl(folder_id id, update change)
    {
        std::stringstream sstr;
        sstr << "<m:UpdateFolder>"
                "<m:FolderChanges>"
                "<t:FolderChange>"
             << id.to_xml() << "<t:Updates>" << change.to_folder_xml()
             << "</t:Updates>"
                "</t:FolderChange>"
                "</m:FolderChanges>"
                "</m:UpdateFolder>";

        auto response = request(sstr.str());
        const auto response_message =
            internal::update_folder_response_message::parse(
                std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        check(!response_message.items().empty(),
              "Expected at least one folder");
        return response_message.items().front();
    }

    folder_id update_folder_impl(folder_id id,
                                 const std::vector<update>& changes)
    {
        std::stringstream sstr;
        sstr << "<m:UpdateFolder>"
                "<m:FolderChanges>"
                "<t:FolderChange>"
             << id.to_xml() << "<t:Updates>";

        for (const auto& change : changes)
        {
            sstr << change.to_folder_xml();
        }

        sstr << "</t:Updates>"
                "</t:FolderChange>"
                "</m:FolderChanges>"
                "</m:UpdateFolder>";

        auto response = request(sstr.str());
        const auto response_message =
            internal::update_folder_response_message::parse(
                std::move(response));
        if (!response_message.success())
        {
            throw exchange_error(response_message.result());
        }
        check(!response_message.items().empty(),
              "Expected at least one folder");
        return response_message.items().front();
    }

    item_id move_item_impl(item_id item, const folder_id& folder)
    {
        std::stringstream sstr;
        sstr << "<m:MoveItem>"
                "<m:ToFolderId>"
             << folder.to_xml()
             << "</m:ToFolderId>"
                "<m:ItemIds>"
             << item.to_xml()
             << "</m:ItemIds>"
                "</m:MoveItem>";

        auto response = request(sstr.str());
        const auto response_messages =
            internal::move_item_response_message::parse(std::move(response));
        if (!response_messages.success())
        {
            throw exchange_error(response_messages.first_error_or_warning());
        }
        check(!response_messages.items().empty(), "Expected at least one item");
        return response_messages.items().front();
    }

    std::vector<item_id> move_item_impl(const std::vector<item_id>& items,
                                        const folder_id& folder)
    {
        std::stringstream sstr;
        sstr << "<m:MoveItem>"
                "<m:ToFolderId>"
             << folder.to_xml()
             << "</m:ToFolderId>"
                "<m:ItemIds>";

        for (const auto& item : items)
        {
            sstr << item.to_xml();
        }

        sstr << "</m:ItemIds>"
                "</m:MoveItem>";

        auto response = request(sstr.str());
        const auto response_messages =
            internal::move_item_response_message::parse(std::move(response));
        if (!response_messages.success())
        {
            throw exchange_error(response_messages.first_error_or_warning());
        }
        check(!response_messages.items().empty(), "Expected at least one item");

        return response_messages.items();
    }

    folder_id move_folder_impl(folder_id folder, const folder_id& target)
    {
        std::stringstream sstr;
        sstr << "<m:MoveFolder>"
                "<m:ToFolderId>"
             << target.to_xml()
             << "</m:ToFolderId>"
                "<m:FolderIds>"
             << folder.to_xml()
             << "</m:FolderIds>"
                "</m:MoveFolder>";

        auto response = request(sstr.str());
        const auto response_messages =
            internal::move_folder_response_message::parse(std::move(response));
        if (!response_messages.success())
        {
            throw exchange_error(response_messages.first_error_or_warning());
        }
        check(!response_messages.items().empty(), "Expected at least one item");
        return response_messages.items().front();
    }

    std::vector<folder_id>
    move_folder_impl(const std::vector<folder_id>& folders,
                     const folder_id& target)
    {
        std::stringstream sstr;
        sstr << "<m:MoveFolder>"
                "<m:ToFolderId>"
             << target.to_xml()
             << "</m:ToFolderId>"
                "<m:FolderIds>";

        for (const auto& folder : folders)
        {
            sstr << folder.to_xml();
        }

        sstr << "</m:FolderIds>"
                "</m:MoveFolder>";

        auto response = request(sstr.str());
        const auto response_messages =
            internal::move_folder_response_message::parse(std::move(response));
        if (!response_messages.success())
        {
            throw exchange_error(response_messages.first_error_or_warning());
        }
        check(!response_messages.items().empty(), "Expected at least one item");

        return response_messages.items();
    }

    resolution_set
    resolve_names_impl(const std::string& name,
                       const std::vector<folder_id>& parent_folder_ids,
                       search_scope scope)
    {
        auto version = get_request_server_version();
        std::stringstream sstr;
        sstr << "<m:ResolveNames "
             << "ReturnFullContactData=\""
             << "true"
             << "\" "
             << "SearchScope=\"" << internal::enum_to_str(scope) << "\" ";

        if (version == server_version::exchange_2010_sp2 ||
            version == server_version::exchange_2013 ||
            version == server_version::exchange_2013_sp1)
        {
            sstr << "ContactDataShape=\"IdOnly\"";
        }
        sstr << ">";
        if (parent_folder_ids.size() > 0)
        {
            sstr << "<ParentFolderIds>";
            for (const auto& id : parent_folder_ids)
            {
                sstr << id.to_xml();
            }
            sstr << "</ParentFolderIds>";
        }
        sstr << "<m:UnresolvedEntry>" << name << "</m:UnresolvedEntry>"
             << "</m:ResolveNames>";
        auto response = request(sstr.str());
        const auto response_message =
            internal::resolve_names_response_message::parse(
                std::move(response));
        if (response_message.result().code ==
                response_code::error_name_resolution_no_results ||
            response_message.result().code ==
                response_code::error_name_resolution_no_mailbox)
        {
            return resolution_set();
        }
        if (response_message.result().cls == response_class::error)
        {
            throw exchange_error(response_message.result());
        }
        return response_message.resolutions();
    }

#ifdef EWS_HAS_VARIANT
    subscription_information
    subscribe_impl(const std::vector<distinguished_folder_id>& ids,
                   const std::vector<event_type>& types, int timeout)
    {
        std::stringstream sstr;
        sstr << "<m:Subscribe>"
             << "<m:PullSubscriptionRequest>"
             << "<t:FolderIds>";
        for (const auto& id : ids)
        {
            sstr << "<t:DistinguishedFolderId Id=\"" << id.id() << "\"/>";
        }
        sstr << "</t:FolderIds>"
             << "<t:EventTypes>";
        for (const auto& type : types)
        {
            sstr << "<t:EventType>" << internal::enum_to_str(type)
                 << "</t:EventType>";
        }
        sstr << "</t:EventTypes>"
             << "<t:Timeout>" << timeout << "</t:Timeout>"
             << "</m:PullSubscriptionRequest>"
             << "</m:Subscribe>";
        auto response = request(sstr.str());
        const auto response_message =
            internal::subscribe_response_message::parse(std::move(response));
        if (response_message.result().cls == response_class::error)
        {
            throw exchange_error(response_message.result());
        }
        return response_message.information();
    }

    void unsubscribe_impl(const std::string& id)
    {
        std::stringstream sstr;
        sstr << "<m:Unsubscribe>"
             << "<m:SubscriptionId>" << id << "</m:SubscriptionId>"
             << "</m:Unsubscribe>";
        auto response = request(sstr.str());
        const auto response_message =
            internal::unsubscribe_response_message::parse(std::move(response));
        if (response_message.result().cls != response_class::success)
        {
            throw exchange_error(response_message.result());
        }
    }

    notification get_events_impl(const std::string& id, const std::string& mark)
    {
        std::stringstream sstr;
        sstr << "<m:GetEvents>"
             << "<m:SubscriptionId>" << id << "</m:SubscriptionId>"
             << "<m:Watermark>" << mark << "</m:Watermark>"
             << "</m:GetEvents>";
        auto response = request(sstr.str());
        const auto response_message =
            internal::get_events_response_message::parse(std::move(response));
        if (response_message.result().cls != response_class::success)
        {
            throw exchange_error(response_message.result());
        }
        return response_message.get_notification();
    }
#endif
};
typedef basic_service<> service;

#if defined(EWS_HAS_NON_BUGGY_TYPE_TRAITS) &&                                  \
    defined(EWS_HAS_CXX17_STATIC_ASSERT)
static_assert(!std::is_default_constructible<service>::value);
static_assert(!std::is_copy_constructible<service>::value);
static_assert(!std::is_copy_assignable<service>::value);
static_assert(std::is_move_constructible<service>::value);
static_assert(std::is_move_assignable<service>::value);
#endif

// Implementations

inline void basic_credentials::certify(internal::http_request* request) const
{
    check(request, "Expected request, got nullptr");

    std::string login = username_ + ":" + password_;
    request->set_option(CURLOPT_USERPWD, login.c_str());
    request->set_option(CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
}

inline void ntlm_credentials::certify(internal::http_request* request) const
{
    check(request, "Expected request, got nullptr");

    // CURLOPT_USERPWD: domain\username:password
    std::string login =
        (domain_.empty() ? "" : domain_ + "\\") + username_ + ":" + password_;
    request->set_option(CURLOPT_USERPWD, login.c_str());
    request->set_option(CURLOPT_HTTPAUTH, CURLAUTH_NTLM);
}

inline void
internal::oauth2_basic::certify(internal::http_request* request) const
{
    check(request, "Expected request, got nullptr");
    // FIXME: we should test expiration here and get another token if the
    // previous one expired, but we can't change the credentials as they are
    // passed const here.
    if (access_token.empty() || expired())
    {
        authenticate(request);
    }

#if LIBCURL_VERSION_NUM >= 0x076100
    request->set_option(CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
    request->set_option(CURLOPT_XOAUTH2_BEARER, access_token.c_str());
#else
    // Set back to default, in case it was changed to a different
    // HTTPAUTH type before.
    request->set_option(CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    request->set_authorization("Bearer " + access_token);
#endif
}

inline void
internal::oauth2_basic::authenticate(internal::http_request* request) const
{
    // Request with the same internal settings of the original one
    internal::http_request req(request->get_handle());

    // curl handle to url-encode the data
    CURL* c = req.get_handle().get();

    char* escaped_client_id =
        curl_easy_escape(c, client_id_.c_str(),
                         internal::numeric_cast<int>(client_id_.length()));

    std::string url =
        "https://login.microsoftonline.com/" + tenant_ + "/oauth2/v2.0/token";
    std::string data;

    data.append("client_id=");
    data.append(escaped_client_id);

    curl_free(escaped_client_id);

    if (!scope_.empty())
    {
        char* escaped_scope = curl_easy_escape(
            c, scope_.c_str(), internal::numeric_cast<int>(scope_.length()));
        data.append("&scope=");
        data.append(escaped_scope);
        curl_free(escaped_scope);
    }

    append_url(c, data);

    // perform a request to get the authentication token
    req.set_option(CURLOPT_URL, url.c_str());
    // HTTPAUTH back to the default option, in case the original
    // handle was set to another HTTPAUTH option that inteferes
    // with options needed for this request
    req.set_option(CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    internal::http_response res = req.send(data);

    std::vector<char> content_vector = res.content();
    std::string content(content_vector.begin(), content_vector.end());

    rapidjson::Document document;
    document.Parse(content.c_str());

    // test for expected JSON contents
    if (document.HasParseError())
    {
        throw exception("OAuth2 Error: JSON Parse Error");
    }

    if (!document.IsObject())
    {
        throw exception("OAuth2 Error: Response is not a JSON Object");
    }

    if (!document.HasMember("access_token") ||
        !document["access_token"].IsString())
    {
        throw exception("OAuth2 Error: no access_token in response\n"
                        "Response: " +
                        content);
    }

    if (!document.HasMember("expires_in") || !document["expires_in"].IsNumber())
    {
        throw exception("OAuth2 Error: no expiration time in response\n"
                        "Response: " +
                        content);
    }

    access_token = document["access_token"].GetString();
    expiration = std::chrono::steady_clock::time_point(
        std::chrono::seconds(document["expires_in"].GetUint64()));
}

inline void
oauth2_resource_owner_password_credentials::append_url(CURL* c,
                                                       std::string& data) const
{
    char* escaped_client_secret =
        curl_easy_escape(c, client_secret_.c_str(),
                         internal::numeric_cast<int>(client_secret_.length()));
    char* escaped_username = curl_easy_escape(
        c, username_.c_str(), internal::numeric_cast<int>(username_.length()));
    char* escaped_password = curl_easy_escape(
        c, password_.c_str(), internal::numeric_cast<int>(password_.length()));

    data.append("&client_secret=");
    data.append(escaped_client_secret);
    data.append("&username=");
    data.append(escaped_username);
    data.append("&password=");
    data.append(escaped_password);
    data.append("&grant_type=password");

    curl_free(escaped_client_secret);
    curl_free(escaped_username);
    curl_free(escaped_password);
}

inline void oauth2_client_credentials::append_url(CURL* c,
                                                  std::string& data) const
{
    char* escaped_client_secret =
        curl_easy_escape(c, client_secret_.c_str(),
                         internal::numeric_cast<int>(client_secret_.length()));
    char* escaped_resource = curl_easy_escape(
        c, resource_.c_str(), internal::numeric_cast<int>(resource_.length()));

    data.append("&client_secret=");
    data.append(escaped_client_secret);
    data.append("&ressource=");
    data.append(escaped_resource);
    data.append("&grant_type=client_credentials");

    curl_free(escaped_resource);
    curl_free(escaped_client_secret);
}

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
inline sync_folder_hierarchy_result
sync_folder_hierarchy_result::parse(internal::http_response&& response)
{
    using rapidxml::internal::compare;

    const auto doc = parse_response(std::move(response));
    auto elem = internal::get_element_by_qname(
        *doc, "SyncFolderHierarchyResponseMessage",
        internal::uri<>::microsoft::messages());

    check(elem, "Expected <SyncFolderHierarchyResponseMessage>");
    auto result = internal::parse_response_class_and_code(*elem);
    if (result.cls == response_class::error)
    {
        throw exchange_error(result);
    }

    auto sync_state_elem = elem->first_node_ns(
        internal::uri<>::microsoft::messages(), "SyncState");
    check(sync_state_elem, "Expected <SyncState> element");
    auto sync_state =
        std::string(sync_state_elem->value(), sync_state_elem->value_size());

    auto includes_last_folder_in_range_elem = elem->first_node_ns(
        internal::uri<>::microsoft::messages(), "IncludesLastFolderInRange");
    check(includes_last_folder_in_range_elem,
          "Expected <IncludesLastFolderInRange> element");
    auto includes_last_folder_in_range = rapidxml::internal::compare(
        includes_last_folder_in_range_elem->value(),
        includes_last_folder_in_range_elem->value_size(), "true",
        strlen("true"));

    auto changes_elem =
        elem->first_node_ns(internal::uri<>::microsoft::messages(), "Changes");
    check(changes_elem, "Expected <Changes> element");
    std::vector<folder> created_folders;
    std::vector<folder> updated_folders;
    std::vector<folder_id> deleted_folder_ids;
    internal::for_each_child_node(
        *changes_elem,
        [&created_folders, &updated_folders,
         &deleted_folder_ids](const rapidxml::xml_node<>& item_elem) {
            if (compare(item_elem.local_name(), item_elem.local_name_size(),
                        "Create", strlen("Create")))
            {
                const auto folder_elem = item_elem.first_node();
                const auto folder = folder::from_xml_element(*folder_elem);
                created_folders.emplace_back(folder);
            }

            if (compare(item_elem.local_name(), item_elem.local_name_size(),
                        "Update", strlen("Update")))
            {
                const auto folder_elem = item_elem.first_node();
                const auto folder = folder::from_xml_element(*folder_elem);
                updated_folders.emplace_back(folder);
            }

            if (compare(item_elem.local_name(), item_elem.local_name_size(),
                        "Delete", strlen("Delete")))
            {
                const auto folder_id_elem = item_elem.first_node_ns(
                    internal::uri<>::microsoft::types(), "FolderId");
                check(folder_id_elem, "Expected <Folder> element");
                const auto folder =
                    folder_id::from_xml_element(*folder_id_elem);
                deleted_folder_ids.emplace_back(folder);
            }
        });

    sync_folder_hierarchy_result response_message(std::move(result));
    response_message.sync_state_ = std::move(sync_state);
    response_message.created_folders_ = std::move(created_folders);
    response_message.updated_folders_ = std::move(updated_folders);
    response_message.deleted_folder_ids_ = std::move(deleted_folder_ids);
    response_message.includes_last_folder_in_range_ =
        includes_last_folder_in_range;

    return response_message;
}
#endif

#ifndef EWS_DOXYGEN_SHOULD_SKIP_THIS
inline sync_folder_items_result
sync_folder_items_result::parse(internal::http_response&& response)
{
    using rapidxml::internal::compare;

    const auto doc = parse_response(std::move(response));
    auto elem =
        internal::get_element_by_qname(*doc, "SyncFolderItemsResponseMessage",
                                       internal::uri<>::microsoft::messages());

    check(elem, "Expected <SyncFolderItemsResponseMessage>");
    auto result = internal::parse_response_class_and_code(*elem);
    if (result.cls == response_class::error)
    {
        throw exchange_error(result);
    }

    auto sync_state_elem = elem->first_node_ns(
        internal::uri<>::microsoft::messages(), "SyncState");
    check(sync_state_elem, "Expected <SyncState> element");
    auto sync_state =
        std::string(sync_state_elem->value(), sync_state_elem->value_size());

    auto includes_last_item_in_range_elem = elem->first_node_ns(
        internal::uri<>::microsoft::messages(), "IncludesLastItemInRange");
    check(includes_last_item_in_range_elem,
          "Expected <IncludesLastItemInRange> element");
    auto includes_last_item_in_range = rapidxml::internal::compare(
        includes_last_item_in_range_elem->value(),
        includes_last_item_in_range_elem->value_size(), "true", strlen("true"));

    auto changes_elem =
        elem->first_node_ns(internal::uri<>::microsoft::messages(), "Changes");
    check(changes_elem, "Expected <Changes> element");
    std::vector<item_id> created_items;
    std::vector<item_id> updated_items;
    std::vector<item_id> deleted_items;
    std::vector<std::pair<item_id, bool>> read_flag_changed;
    internal::for_each_child_node(
        *changes_elem,
        [&created_items, &updated_items, &deleted_items,
         &read_flag_changed](const rapidxml::xml_node<>& item_elem) {
            if (compare(item_elem.local_name(), item_elem.local_name_size(),
                        "Create", strlen("Create")))
            {
                const auto item_id_elem = item_elem.first_node()->first_node_ns(
                    internal::uri<>::microsoft::types(), "ItemId");
                check(item_id_elem, "Expected <ItemId> element");
                const auto item_id = item_id::from_xml_element(*item_id_elem);
                created_items.emplace_back(item_id);
            }

            if (compare(item_elem.local_name(), item_elem.local_name_size(),
                        "Update", strlen("Update")))
            {
                const auto item_id_elem = item_elem.first_node()->first_node_ns(
                    internal::uri<>::microsoft::types(), "ItemId");
                check(item_id_elem, "Expected <ItemId> element");
                const auto item_id = item_id::from_xml_element(*item_id_elem);
                updated_items.emplace_back(item_id);
            }

            if (compare(item_elem.local_name(), item_elem.local_name_size(),
                        "Delete", strlen("Delete")))
            {
                const auto item_id_elem = item_elem.first_node_ns(
                    internal::uri<>::microsoft::types(), "ItemId");
                check(item_id_elem, "Expected <ItemId> element");
                const auto item_id = item_id::from_xml_element(*item_id_elem);
                deleted_items.emplace_back(item_id);
            }

            if (compare(item_elem.local_name(), item_elem.local_name_size(),
                        "ReadFlagChange", strlen("ReadFlagChange")))
            {
                const auto item_id_elem = item_elem.first_node_ns(
                    internal::uri<>::microsoft::types(), "ItemId");
                check(item_id_elem, "Expected <ItemId> element");

                const auto read_elem = item_elem.first_node_ns(
                    internal::uri<>::microsoft::types(), "IsRead");
                check(read_elem, "Expected <IsRead> element");

                const auto item_id = item_id::from_xml_element(*item_id_elem);

                const bool read =
                    compare(read_elem->value(), read_elem->value_size(), "true",
                            strlen("true"));

                read_flag_changed.emplace_back(std::make_pair(item_id, read));
            }
        });

    sync_folder_items_result response_message(std::move(result));
    response_message.sync_state_ = std::move(sync_state);
    response_message.created_items_ = std::move(created_items);
    response_message.updated_items_ = std::move(updated_items);
    response_message.deleted_items_ = std::move(deleted_items);
    response_message.read_flag_changed_ = std::move(read_flag_changed);
    response_message.includes_last_item_in_range_ = includes_last_item_in_range;

    return response_message;
}
#endif

namespace internal
{
    inline create_folder_response_message
    create_folder_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto elem = get_element_by_qname(*doc, "CreateFolderResponseMessage",
                                         uri<>::microsoft::messages());

        check(elem, "Expected <CreateFolderResponseMessage>");
        auto result = parse_response_class_and_code(*elem);
        auto item_ids = std::vector<folder_id>();
        auto items_elem =
            elem->first_node_ns(uri<>::microsoft::messages(), "Folders");
        check(items_elem, "Expected <Folders> element");

        for_each_child_node(
            *items_elem, [&item_ids](const rapidxml::xml_node<>& item_elem) {
                auto item_id_elem = item_elem.first_node();
                check(item_id_elem, "Expected <FolderId> element");
                item_ids.emplace_back(
                    folder_id::from_xml_element(*item_id_elem));
            });
        return create_folder_response_message(std::move(result),
                                              std::move(item_ids));
    }

    inline create_item_response_message
    create_item_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto elem = get_element_by_qname(*doc, "CreateItemResponseMessage",
                                         uri<>::microsoft::messages());

        check(elem, "Expected <CreateItemResponseMessage>");
        auto result = parse_response_class_and_code(*elem);
        auto item_ids = std::vector<item_id>();
        auto items_elem =
            elem->first_node_ns(uri<>::microsoft::messages(), "Items");
        check(items_elem, "Expected <Items> element");

        for_each_child_node(
            *items_elem, [&item_ids](const rapidxml::xml_node<>& item_elem) {
                auto item_id_elem = item_elem.first_node();
                check(item_id_elem, "Expected <ItemId> element");
                item_ids.emplace_back(item_id::from_xml_element(*item_id_elem));
            });
        return create_item_response_message(std::move(result),
                                            std::move(item_ids));
    }

    inline find_folder_response_message
    find_folder_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto elem = get_element_by_qname(*doc, "FindFolderResponseMessage",
                                         uri<>::microsoft::messages());

        check(elem, "Expected <FindFolderResponseMessage>");
        auto result = parse_response_class_and_code(*elem);

        auto root_folder =
            elem->first_node_ns(uri<>::microsoft::messages(), "RootFolder");

        auto items_elem =
            root_folder->first_node_ns(uri<>::microsoft::types(), "Folders");
        check(items_elem, "Expected <Folders> element");

        auto items = std::vector<folder_id>();
        for (auto item_elem = items_elem->first_node(); item_elem;
             item_elem = item_elem->next_sibling())
        {
            // TODO: Check that item is 'FolderId'
            check(item_elem, "Expected an element");
            auto item_id_elem = item_elem->first_node();
            check(item_id_elem, "Expected <FolderId> element");
            items.emplace_back(folder_id::from_xml_element(*item_id_elem));
        }
        return find_folder_response_message(std::move(result),
                                            std::move(items));
    }

    inline find_item_response_message
    find_item_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto elem = get_element_by_qname(*doc, "FindItemResponseMessage",
                                         uri<>::microsoft::messages());

        check(elem, "Expected <FindItemResponseMessage>");
        auto result = parse_response_class_and_code(*elem);

        auto root_folder =
            elem->first_node_ns(uri<>::microsoft::messages(), "RootFolder");

        auto items_elem =
            root_folder->first_node_ns(uri<>::microsoft::types(), "Items");
        check(items_elem, "Expected <Items> element");

        auto items = std::vector<item_id>();
        for (auto item_elem = items_elem->first_node(); item_elem;
             item_elem = item_elem->next_sibling())
        {
            check(item_elem, "Expected an element");
            auto item_id_elem = item_elem->first_node();
            check(item_id_elem, "Expected <ItemId> element");
            items.emplace_back(item_id::from_xml_element(*item_id_elem));
        }
        return find_item_response_message(std::move(result), std::move(items));
    }

    inline find_calendar_item_response_message
    find_calendar_item_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto elem = get_element_by_qname(*doc, "FindItemResponseMessage",
                                         uri<>::microsoft::messages());

        check(elem, "Expected <FindItemResponseMessage>");
        auto result = parse_response_class_and_code(*elem);

        check_response_message_for_error(*elem);

        auto root_folder =
            elem->first_node_ns(uri<>::microsoft::messages(), "RootFolder");
        check(root_folder, "Expected <RootFolder> element");

        auto items_elem =
            root_folder->first_node_ns(uri<>::microsoft::types(), "Items");
        check(items_elem, "Expected <Items> element");

        auto items = std::vector<calendar_item>();
        for_each_child_node(
            *items_elem, [&items](const rapidxml::xml_node<>& item_elem) {
                items.emplace_back(calendar_item::from_xml_element(item_elem));
            });
        return find_calendar_item_response_message(std::move(result),
                                                   std::move(items));
    }

    inline update_item_response_message
    update_item_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto elem = get_element_by_qname(*doc, "UpdateItemResponseMessage",
                                         uri<>::microsoft::messages());

        check(elem, "Expected <UpdateItemResponseMessage>");
        auto result = parse_response_class_and_code(*elem);

        auto items_elem =
            elem->first_node_ns(uri<>::microsoft::messages(), "Items");
        check(items_elem, "Expected <Items> element");

        auto items = std::vector<item_id>();
        for (auto item_elem = items_elem->first_node(); item_elem;
             item_elem = item_elem->next_sibling())
        {
            check(item_elem, "Expected an element");
            auto item_id_elem = item_elem->first_node();
            check(item_id_elem, "Expected <ItemId> element");
            items.emplace_back(item_id::from_xml_element(*item_id_elem));
        }
        return update_item_response_message(std::move(result),
                                            std::move(items));
    }

    inline update_folder_response_message
    update_folder_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto elem = get_element_by_qname(*doc, "UpdateFolderResponseMessage",
                                         uri<>::microsoft::messages());

        check(elem, "Expected <UpdateFolderResponseMessage>");
        auto result = parse_response_class_and_code(*elem);

        auto folders_elem =
            elem->first_node_ns(uri<>::microsoft::messages(), "Folders");
        check(folders_elem, "Expected <Folders> element");

        auto folders = std::vector<folder_id>();
        for (auto folder_elem = folders_elem->first_node(); folder_elem;
             folder_elem = folder_elem->next_sibling())
        {
            check(folder_elem, "Expected an element");
            auto folder_id_elem = folder_elem->first_node();
            check(folder_id_elem, "Expected <FolderId> element");
            folders.emplace_back(folder_id::from_xml_element(*folder_id_elem));
        }
        return update_folder_response_message(std::move(result),
                                              std::move(folders));
    }

    inline get_folder_response_message
    get_folder_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto elem = get_element_by_qname(*doc, "GetFolderResponseMessage",
                                         uri<>::microsoft::messages());
        check(elem, "Expected <GetFolderResponseMessage>");
        auto result = parse_response_class_and_code(*elem);
        auto items_elem =
            elem->first_node_ns(uri<>::microsoft::messages(), "Folders");
        check(items_elem, "Expected <Folders> element");
        auto items = std::vector<folder>();
        for_each_child_node(
            *items_elem, [&items](const rapidxml::xml_node<>& item_elem) {
                items.emplace_back(folder::from_xml_element(item_elem));
            });
        return get_folder_response_message(std::move(result), std::move(items));
    }

    inline get_room_lists_response_message
    get_room_lists_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto elem = get_element_by_qname(*doc, "GetRoomListsResponse",
                                         uri<>::microsoft::messages());
        check(elem, "Expected <GetRoomListsResponse>");

        auto result = parse_response_class_and_code(*elem);
        if (result.cls == response_class::error)
        {
            throw exchange_error(result);
        }

        auto room_lists = std::vector<mailbox>();
        auto items_elem =
            elem->first_node_ns(uri<>::microsoft::messages(), "RoomLists");
        check(items_elem, "Expected <RoomLists> element");

        for_each_child_node(
            *items_elem, [&room_lists](const rapidxml::xml_node<>& item_elem) {
                room_lists.emplace_back(mailbox::from_xml_element(item_elem));
            });
        return get_room_lists_response_message(std::move(result),
                                               std::move(room_lists));
    }

    inline get_rooms_response_message
    get_rooms_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto elem = get_element_by_qname(*doc, "GetRoomsResponse",
                                         uri<>::microsoft::messages());

        check(elem, "Expected <GetRoomsResponse>");
        auto result = parse_response_class_and_code(*elem);
        auto rooms = std::vector<mailbox>();
        auto items_elem =
            elem->first_node_ns(uri<>::microsoft::messages(), "Rooms");
        if (!items_elem)
        {
            return get_rooms_response_message(std::move(result),
                                              std::move(rooms));
        }

        for_each_child_node(
            *items_elem, [&rooms](const rapidxml::xml_node<>& item_elem) {
                auto room_elem =
                    item_elem.first_node_ns(uri<>::microsoft::types(), "Id");
                check(room_elem, "Expected <Id> element");
                rooms.emplace_back(mailbox::from_xml_element(*room_elem));
            });
        return get_rooms_response_message(std::move(result), std::move(rooms));
    }

    inline folder_response_message
    folder_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));

        auto response_messages = get_element_by_qname(
            *doc, "ResponseMessages", uri<>::microsoft::messages());
        check(response_messages, "Expected <ResponseMessages> node");

        std::vector<folder_response_message::response_message> messages;
        for_each_child_node(
            *response_messages, [&](const rapidxml::xml_node<>& node) {
                auto result = parse_response_class_and_code(node);

                auto items_elem =
                    node.first_node_ns(uri<>::microsoft::messages(), "Folders");
                check(items_elem, "Expected <Folders> element");

                auto items = std::vector<folder>();
                for_each_child_node(
                    *items_elem,
                    [&items](const rapidxml::xml_node<>& item_elem) {
                        items.emplace_back(folder::from_xml_element(item_elem));
                    });

                messages.emplace_back(
                    std::make_tuple(result.cls, result.code, std::move(items)));
            });

        return folder_response_message(std::move(messages));
    }

    template <typename ItemType>
    inline get_item_response_message<ItemType>
    get_item_response_message<ItemType>::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto elem = get_element_by_qname(*doc, "GetItemResponseMessage",
                                         uri<>::microsoft::messages());
        check(elem, "Expected <GetItemResponseMessage>");
        auto result = parse_response_class_and_code(*elem);
        auto items_elem =
            elem->first_node_ns(uri<>::microsoft::messages(), "Items");
        check(items_elem, "Expected <Items> element");
        auto items = std::vector<ItemType>();
        for_each_child_node(
            *items_elem, [&items](const rapidxml::xml_node<>& item_elem) {
                items.emplace_back(ItemType::from_xml_element(item_elem));
            });
        return get_item_response_message(std::move(result), std::move(items));
    }

    inline move_item_response_message
    move_item_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));

        auto response_messages = get_element_by_qname(
            *doc, "ResponseMessages", uri<>::microsoft::messages());
        check(response_messages, "Expected <ResponseMessages> node");

        using rapidxml::internal::compare;

        std::vector<response_message_with_ids::response_message> messages;
        for_each_child_node(
            *response_messages, [&](const rapidxml::xml_node<>& node) {
                check(compare(node.local_name(), node.local_name_size(),
                              "MoveItemResponseMessage",
                              strlen("MoveItemResponseMessage")),
                      "Expected <MoveItemResponseMessage> element");

                auto result = parse_response_class_and_code(node);

                auto items_elem =
                    node.first_node_ns(uri<>::microsoft::messages(), "Items");
                check(items_elem, "Expected <Items> element");

                auto items = std::vector<item_id>();
                for_each_child_node(
                    *items_elem,
                    [&items](const rapidxml::xml_node<>& item_elem) {
                        auto item_id = item_elem.first_node_ns(
                            uri<>::microsoft::types(), "ItemId");
                        check(item_id, "Expected <ItemId> element");
                        items.emplace_back(item_id::from_xml_element(*item_id));
                    });

                messages.emplace_back(
                    std::make_tuple(result.cls, result.code, std::move(items)));
            });

        return move_item_response_message(std::move(messages));
    }

    inline move_folder_response_message
    move_folder_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));

        auto response_messages = get_element_by_qname(
            *doc, "ResponseMessages", uri<>::microsoft::messages());
        check(response_messages, "Expected <ResponseMessages> node");

        using rapidxml::internal::compare;

        std::vector<response_message_with_ids::response_message> messages;
        for_each_child_node(
            *response_messages, [&](const rapidxml::xml_node<>& node) {
                check(compare(node.local_name(), node.local_name_size(),
                              "MoveFolderResponseMessage",
                              strlen("MoveFolderResponseMessage")),
                      "Expected <MoveFolderResponseMessage> element");

                auto result = parse_response_class_and_code(node);

                auto folders_elem =
                    node.first_node_ns(uri<>::microsoft::messages(), "Folders");
                check(folders_elem, "Expected <Folders> element");

                auto folders = std::vector<folder_id>();
                for_each_child_node(
                    *folders_elem,
                    [&folders](const rapidxml::xml_node<>& folder_elem) {
                        auto folder_id = folder_elem.first_node_ns(
                            uri<>::microsoft::types(), "FolderId");
                        check(folder_id, "Expected <FolderId> element");
                        folders.emplace_back(
                            folder_id::from_xml_element(*folder_id));
                    });

                messages.emplace_back(std::make_tuple(result.cls, result.code,
                                                      std::move(folders)));
            });

        return move_folder_response_message(std::move(messages));
    }

    template <typename ItemType>
    inline item_response_messages<ItemType>
    item_response_messages<ItemType>::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));

        auto response_messages = get_element_by_qname(
            *doc, "ResponseMessages", uri<>::microsoft::messages());
        check(response_messages, "Expected <ResponseMessages> node");

        std::vector<item_response_messages::response_message> messages;
        for_each_child_node(
            *response_messages, [&](const rapidxml::xml_node<>& node) {
                auto result = parse_response_class_and_code(node);

                auto items_elem =
                    node.first_node_ns(uri<>::microsoft::messages(), "Items");
                check(items_elem, "Expected <Items> element");

                auto items = std::vector<ItemType>();
                for_each_child_node(
                    *items_elem,
                    [&items](const rapidxml::xml_node<>& item_elem) {
                        items.emplace_back(
                            ItemType::from_xml_element(item_elem));
                    });

                messages.emplace_back(
                    std::make_tuple(result.cls, result.code, std::move(items)));
            });

        return item_response_messages(std::move(messages));
    }

    inline std::vector<delegate_user> delegate_response_message::parse_users(
        const rapidxml::xml_node<>& response_element)
    {
        using rapidxml::internal::compare;

        std::vector<delegate_user> delegate_users;
        for_each_child_node(
            response_element, [&](const rapidxml::xml_node<>& node) {
                if (compare(node.local_name(), node.local_name_size(),
                            "ResponseMessages", strlen("ResponseMessages")))
                {
                    for_each_child_node(
                        node, [&](const rapidxml::xml_node<>& msg) {
                            for_each_child_node(
                                msg,
                                [&](const rapidxml::xml_node<>& msg_content) {
                                    if (compare(msg_content.local_name(),
                                                msg_content.local_name_size(),
                                                "DelegateUser",
                                                strlen("DelegateUser")))
                                    {
                                        delegate_users.emplace_back(
                                            delegate_user::from_xml_element(
                                                msg_content));
                                    }
                                });
                        });
                }
            });
        return delegate_users;
    }

    inline add_delegate_response_message
    add_delegate_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto response_elem = get_element_by_qname(*doc, "AddDelegateResponse",
                                                  uri<>::microsoft::messages());
        check(response_elem, "Expected <AddDelegateResponse>");
        auto result = parse_response_class_and_code(*response_elem);

        std::vector<delegate_user> delegates;
        if (result.code == response_code::no_error)
        {
            delegates = delegate_response_message::parse_users(*response_elem);
        }
        return add_delegate_response_message(std::move(result),
                                             std::move(delegates));
    }

    inline get_delegate_response_message
    get_delegate_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto response_elem = get_element_by_qname(*doc, "GetDelegateResponse",
                                                  uri<>::microsoft::messages());
        check(response_elem, "Expected <GetDelegateResponse>");
        auto result = parse_response_class_and_code(*response_elem);

        std::vector<delegate_user> delegates;
        if (result.code == response_code::no_error)
        {
            delegates = delegate_response_message::parse_users(*response_elem);
        }
        return get_delegate_response_message(std::move(result),
                                             std::move(delegates));
    }

    inline remove_delegate_response_message
    remove_delegate_response_message::parse(http_response&& response)
    {
        const auto doc = parse_response(std::move(response));
        auto resp = get_element_by_qname(*doc, "RemoveDelegateResponse",
                                         uri<>::microsoft::messages());
        check(resp, "Expected <RemoveDelegateResponse>");

        auto result = parse_response_class_and_code(*resp);
        if (result.code == response_code::no_error)
        {
            // We still need to check each individual element in
            // <ResponseMessages> for errors ¯\_(⊙︿⊙)_/¯

            using rapidxml::internal::compare;

            for_each_child_node(*resp, [](const rapidxml::xml_node<>& elem) {
                if (compare(elem.local_name(), elem.local_name_size(),
                            "ResponseMessages", strlen("ResponseMessages")))
                {
                    for_each_child_node(
                        elem, [](const rapidxml::xml_node<>& msg) {
                            auto response_class_attr =
                                msg.first_attribute("ResponseClass");
                            if (compare(response_class_attr->value(),
                                        response_class_attr->value_size(),
                                        "Error", 5))
                            {
                                // Okay, we got an error. Additional
                                // context about the error might be
                                // available in <m:MessageText>. Throw
                                // directly from here.

                                auto code = response_code::no_error;

                                auto rcode_elem = msg.first_node_ns(
                                    uri<>::microsoft::messages(),
                                    "ResponseCode");
                                check(rcode_elem,
                                      "Expected <ResponseCode> element");
                                code =
                                    str_to_response_code(rcode_elem->value());

                                auto message_text = msg.first_node_ns(
                                    uri<>::microsoft::messages(),
                                    "MessageText");

                                if (message_text)
                                {
                                    throw exchange_error(
                                        code, std::string(
                                                  message_text->value(),
                                                  message_text->value_size()));
                                }

                                throw exchange_error(code);
                            }
                        });
                }
            });
        }

        return remove_delegate_response_message(std::move(result));
    }

    inline resolve_names_response_message
    resolve_names_response_message::parse(http_response&& response)
    {
        using rapidxml::internal::compare;
        const auto doc = parse_response(std::move(response));
        auto response_elem = get_element_by_qname(
            *doc, "ResolveNamesResponseMessage", uri<>::microsoft::messages());
        check(response_elem, "Expected <ResolveNamesResponseMessage>");
        auto result = parse_response_class_and_code(*response_elem);

        resolution_set resolutions;
        if (result.code == response_code::no_error ||
            result.code ==
                response_code::error_name_resolution_multiple_results)
        {
            auto resolution_set_element = response_elem->first_node_ns(
                uri<>::microsoft::messages(), "ResolutionSet");
            check(resolution_set_element, "Expected <ResolutionSet> element");

            for (auto attr = resolution_set_element->first_attribute();
                 attr != nullptr; attr = attr->next_attribute())
            {
                if (compare("IndexedPagingOffset",
                            strlen("IndexedPagingOffset"), attr->local_name(),
                            attr->local_name_size()))
                {
                    resolutions.indexed_paging_offset =
                        std::stoi(resolution_set_element
                                      ->first_attribute("IndexedPagingOffset")
                                      ->value());
                }
                if (compare("NumeratorOffset", strlen("NumeratorOffset"),
                            attr->local_name(), attr->local_name_size()))
                {
                    resolutions.numerator_offset =
                        std::stoi(resolution_set_element
                                      ->first_attribute("NumeratorOffset")
                                      ->value());
                }
                if (compare("AbsoluteDenominator",
                            strlen("AbsoluteDenominator"), attr->local_name(),
                            attr->local_name_size()))
                {
                    resolutions.absolute_denominator =
                        std::stoi(resolution_set_element
                                      ->first_attribute("AbsoluteDenominator")
                                      ->value());
                }
                if (compare("IncludesLastItemInRange",
                            strlen("IncludesLastItemInRange"),
                            attr->local_name(), attr->local_name_size()))
                {
                    auto includes =
                        resolution_set_element
                            ->first_attribute("IncludesLastItemInRange")
                            ->value();
                    if (includes)
                    {
                        resolutions.includes_last_item_in_range = true;
                    }
                    else
                    {
                        resolutions.includes_last_item_in_range = false;
                    }
                }
                if (compare("TotalItemsInView", strlen("TotalItemsInView"),
                            attr->local_name(), attr->local_name_size()))
                {
                    resolutions.total_items_in_view =
                        std::stoi(resolution_set_element
                                      ->first_attribute("TotalItemsInView")
                                      ->value());
                }
            }

            for (auto res = resolution_set_element->first_node_ns(
                     uri<>::microsoft::types(), "Resolution");
                 res; res = res->next_sibling())
            {
                check(res, "Expected <Resolution> element");
                resolution r;

                if (compare("Mailbox", strlen("Mailbox"),
                            res->first_node()->local_name(),
                            res->first_node()->local_name_size()))
                {
                    auto mailbox_elem = res->first_node("t:Mailbox");
                    r.mailbox = mailbox::from_xml_element(*mailbox_elem);
                }
                if (compare("Contact", strlen("Contact"),
                            res->last_node()->local_name(),
                            res->last_node()->local_name_size()))
                {
                    auto contact_elem = res->last_node("t:Contact");
                    auto directory_id_elem = contact_elem->first_node_ns(
                        internal::uri<>::microsoft::types(), "DirectoryId");

                    if (directory_id_elem)
                    {
                        directory_id id(directory_id_elem->value());
                        r.directory_id = id;
                    }

                    r.contact = std::make_shared<ews::contact>(
                        contact::from_xml_element_without_item_id(
                            *contact_elem));
                }

                resolutions.resolutions.emplace_back(r);
            }
        }
        return resolve_names_response_message(std::move(result),
                                              std::move(resolutions));
    }

#ifdef EWS_HAS_VARIANT
    inline subscribe_response_message
    subscribe_response_message::parse(http_response&& response)
    {
        using rapidxml::internal::compare;
        const auto doc = parse_response(std::move(response));
        auto response_elem = get_element_by_qname(
            *doc, "SubscribeResponseMessage", uri<>::microsoft::messages());
        check(response_elem, "Expected <SubscribeResponseMessage>");
        auto result = parse_response_class_and_code(*response_elem);

        std::string id;
        std::string mark;
        if (result.code == response_code::no_error)
        {

            id = response_elem
                     ->first_node_ns(uri<>::microsoft::messages(),
                                     "SubscriptionId")
                     ->value();

            mark =
                response_elem
                    ->first_node_ns(uri<>::microsoft::messages(), "Watermark")
                    ->value();
        }
        return subscribe_response_message(std::move(result),
                                          subscription_information(id, mark));
    }

    inline unsubscribe_response_message
    unsubscribe_response_message::parse(http_response&& response)
    {
        using rapidxml::internal::compare;
        const auto doc = parse_response(std::move(response));
        auto response_elem = get_element_by_qname(
            *doc, "UnsubscribeResponseMessage", uri<>::microsoft::messages());
        check(response_elem, "Expected <UnsubscribeResponseMessage>");
        auto result = parse_response_class_and_code(*response_elem);

        return unsubscribe_response_message(std::move(result));
    }

    inline get_events_response_message
    get_events_response_message::parse(http_response&& response)
    {
        using rapidxml::internal::compare;
        const auto doc = parse_response(std::move(response));
        auto response_elem = get_element_by_qname(
            *doc, "GetEventsResponseMessage", uri<>::microsoft::messages());
        check(response_elem, "Expected <GetEventsResponseMessage>");
        auto result = parse_response_class_and_code(*response_elem);

        notification n;
        if (result.code == response_code::no_error)
        {
            auto notification_element = response_elem->first_node_ns(
                uri<>::microsoft::messages(), "Notification");
            check(notification_element, "Expected <Notification> element");

            n.subscription_id =
                notification_element
                    ->first_node_ns(uri<>::microsoft::types(), "SubscriptionId")
                    ->value();

            n.previous_watermark =
                notification_element
                    ->first_node_ns(uri<>::microsoft::types(),
                                    "PreviousWatermark")
                    ->value();

            n.more_events =
                notification_element
                    ->first_node_ns(uri<>::microsoft::types(), "MoreEvents")
                    ->value();

            if (notification_element->first_node_ns(uri<>::microsoft::types(),
                                                    "StatusEvent") != nullptr)
            {
                auto e = notification_element->first_node_ns(
                    uri<>::microsoft::types(), "StatusEvent");
                status_event s = status_event::from_xml_element(*e);
                n.events.emplace_back(s);
            }
            else
            {
                for (auto res = notification_element->first_node_ns(
                         uri<>::microsoft::types(), "CopiedEvent");
                     res; res = res->next_sibling())
                {
                    copied_event c = copied_event::from_xml_element(*res);
                    n.events.emplace_back(c);
                }
                for (auto res = notification_element->first_node_ns(
                         uri<>::microsoft::types(), "CreatedEvent");
                     res; res = res->next_sibling())
                {
                    created_event c = created_event::from_xml_element(*res);
                    n.events.emplace_back(c);
                }
                for (auto res = notification_element->first_node_ns(
                         uri<>::microsoft::types(), "DeletedEvent");
                     res; res = res->next_sibling())
                {
                    deleted_event d = deleted_event::from_xml_element(*res);
                    n.events.emplace_back(d);
                }
                for (auto res = notification_element->first_node_ns(
                         uri<>::microsoft::types(), "ModifiedEvent");
                     res; res = res->next_sibling())
                {
                    modified_event d = modified_event::from_xml_element(*res);
                    n.events.emplace_back(d);
                }
                for (auto res = notification_element->first_node_ns(
                         uri<>::microsoft::types(), "MovedEvent");
                     res; res = res->next_sibling())
                {
                    moved_event m = moved_event::from_xml_element(*res);
                    n.events.emplace_back(m);
                }
                for (auto res = notification_element->first_node_ns(
                         uri<>::microsoft::types(), "NewMailEvent");
                     res; res = res->next_sibling())
                {
                    new_mail_event m = new_mail_event::from_xml_element(*res);
                    n.events.emplace_back(m);
                }
                for (auto res = notification_element->first_node_ns(
                         uri<>::microsoft::types(), "FreeBusyChangedEvent");
                     res; res = res->next_sibling())
                {
                    free_busy_changed_event f =
                        free_busy_changed_event::from_xml_element(*res);
                    n.events.emplace_back(f);
                }
            }
        }
        return get_events_response_message(std::move(result), std::move(n));
    }
#endif
} // namespace internal

//! \brief Creates a new <tt>\<ItemAttachment></tt> from a given item.
//!
//! It is not necessary for the item to already exist in the Exchange
//! store. If it doesn't, it will be automatically created.
inline attachment attachment::from_item(const item& the_item,
                                        const std::string& name)
{
    // Creating a new <ItemAttachment> with the <CreateAttachment>
    // method is pretty similar to a <CreateItem> method call. However,
    // most of the times we do not want to create item attachments out
    // of thin air but attach an _existing_ item.
    //
    // If we want create an attachment from an existing item, we need
    // to first call <GetItem> before we call <CreateItem> and put the
    // complete item from the response into the <CreateAttachment>
    // call.
    //
    // There is a shortcut for Calendar, E-mail message items, and Posting
    // notes: use <BaseShape>IdOnly</BaseShape> and <AdditionalProperties>
    // with item::MimeContent in <GetItem> call, remove <ItemId> from the
    // response and pass that to <CreateAttachment>.

#if 0
        const auto item_class = the_item.get_item_class();
        if (       item_class == ""          // Calendar items
                || item_class == "IPM.Note"  // E-mail messages
                || item_class == "IPM.Post"  // Posting notes in a folder
            )
        {
            auto mime_content_node = props.get_node("MimeContent");
        }
#endif

    auto& props = the_item.xml();

// Filter out read-only property paths
#ifdef EWS_HAS_INITIALIZER_LISTS
    for (const auto& property_name :
         // item
         {"ItemId", "ParentFolderId", "DateTimeReceived", "Size", "IsSubmitted",
          "IsDraft", "IsFromMe", "IsResend", "IsUnmodified", "DateTimeSent",
          "DateTimeCreated", "ResponseObjects", "DisplayCc", "DisplayTo",
          "HasAttachments", "EffectiveRights", "LastModifiedName",
          "LastModifiedTime", "IsAssociated", "WebClientReadFormQueryString",
          "WebClientEditFormQueryString", "ConversationId", "InstanceKey",

          // message
          "ConversationIndex", "ConversationTopic"})
#else
    std::vector<std::string> properties;

    // item
    properties.push_back("ItemId");
    properties.push_back("ParentFolderId");
    properties.push_back("DateTimeReceived");
    properties.push_back("Size");
    properties.push_back("IsSubmitted");
    properties.push_back("IsDraft");
    properties.push_back("IsFromMe");
    properties.push_back("IsResend");
    properties.push_back("IsUnmodified");
    properties.push_back("DateTimeSent");
    properties.push_back("DateTimeCreated");
    properties.push_back("ResponseObjects");
    properties.push_back("DisplayCc");
    properties.push_back("DisplayTo");
    properties.push_back("HasAttachments");
    properties.push_back("EffectiveRights");
    properties.push_back("LastModifiedName");
    properties.push_back("LastModifiedTime");
    properties.push_back("IsAssociated");
    properties.push_back("WebClientReadFormQueryString");
    properties.push_back("WebClientEditFormQueryString");
    properties.push_back("ConversationId");
    properties.push_back("InstanceKey");

    // message
    properties.push_back("ConversationIndex");
    properties.push_back("ConversationTopic");

    for (const auto& property_name : properties)
#endif
    {
        auto node = props.get_node(property_name);
        if (node)
        {
            auto parent = node->parent();
            check(parent, "Expected node to have a parent node");
            parent->remove_node(node);
        }
    }

    auto obj = attachment();
    obj.type_ = type::item;

    using internal::create_node;
    auto& attachment_node =
        create_node(*obj.xml_.document(), "t:ItemAttachment");
    create_node(attachment_node, "t:Name", name);
    props.append_to(attachment_node);

    return obj;
}

inline std::string delegate_user::delegate_permissions::to_xml() const
{
    std::stringstream sstr;
    sstr << "<t:DelegatePermissions>";
    sstr << "<t:CalendarFolderPermissionLevel>"
         << internal::enum_to_str(calendar_folder)
         << "</t:CalendarFolderPermissionLevel>";

    sstr << "<t:TasksFolderPermissionLevel>"
         << internal::enum_to_str(tasks_folder)
         << "</t:TasksFolderPermissionLevel>";

    sstr << "<t:InboxFolderPermissionLevel>"
         << internal::enum_to_str(inbox_folder)
         << "</t:InboxFolderPermissionLevel>";

    sstr << "<t:ContactsFolderPermissionLevel>"
         << internal::enum_to_str(contacts_folder)
         << "</t:ContactsFolderPermissionLevel>";

    sstr << "<t:NotesFolderPermissionLevel>"
         << internal::enum_to_str(notes_folder)
         << "</t:NotesFolderPermissionLevel>";

    sstr << "<t:JournalFolderPermissionLevel>"
         << internal::enum_to_str(journal_folder)
         << "</t:JournalFolderPermissionLevel>";
    sstr << "</t:DelegatePermissions>";
    return sstr.str();
}

inline delegate_user::delegate_permissions
delegate_user::delegate_permissions::from_xml_element(
    const rapidxml::xml_node<char>& elem)
{
    using rapidxml::internal::compare;

    delegate_permissions perms;
    for (auto node = elem.first_node(); node; node = node->next_sibling())
    {
        if (compare(node->local_name(), node->local_name_size(),
                    "CalendarFolderPermissionLevel",
                    strlen("CalendarFolderPermissionLevel")))
        {
            perms.calendar_folder = internal::str_to_permission_level(
                std::string(node->value(), node->value_size()));
        }
        else if (compare(node->local_name(), node->local_name_size(),
                         "TasksFolderPermissionLevel",
                         strlen("TasksFolderPermissionLevel")))
        {
            perms.tasks_folder = internal::str_to_permission_level(
                std::string(node->value(), node->value_size()));
        }
        else if (compare(node->local_name(), node->local_name_size(),
                         "InboxFolderPermissionLevel",
                         strlen("InboxFolderPermissionLevel")))
        {
            perms.inbox_folder = internal::str_to_permission_level(
                std::string(node->value(), node->value_size()));
        }
        else if (compare(node->local_name(), node->local_name_size(),
                         "ContactsFolderPermissionLevel",
                         strlen("ContactsFolderPermissionLevel")))
        {
            perms.contacts_folder = internal::str_to_permission_level(
                std::string(node->value(), node->value_size()));
        }
        else if (compare(node->local_name(), node->local_name_size(),
                         "NotesFolderPermissionLevel",
                         strlen("NotesFolderPermissionLevel")))
        {
            perms.notes_folder = internal::str_to_permission_level(
                std::string(node->value(), node->value_size()));
        }
        else if (compare(node->local_name(), node->local_name_size(),
                         "JournalFolderPermissionLevel",
                         strlen("JournalFolderPermissionLevel")))
        {
            perms.journal_folder = internal::str_to_permission_level(
                std::string(node->value(), node->value_size()));
        }
        else
        {
            throw exception(
                "Unexpected child element in <DelegatePermissions>");
        }
    }
    return perms;
}

inline std::unique_ptr<recurrence_pattern>
recurrence_pattern::from_xml_element(const rapidxml::xml_node<>& elem)
{
    using rapidxml::internal::compare;
    using namespace internal;

    check(compare(elem.local_name(), elem.local_name_size(), "Recurrence",
                  strlen("Recurrence")),
          "Expected a <Recurrence> element");

    auto node = elem.first_node_ns(uri<>::microsoft::types(),
                                   "AbsoluteYearlyRecurrence");
    if (node)
    {
        auto mon = month::jan;
        uint32_t day_of_month = 0U;

        for (auto child = node->first_node(); child;
             child = child->next_sibling())
        {
            if (compare(child->local_name(), child->local_name_size(), "Month",
                        strlen("Month")))
            {
                mon = str_to_month(
                    std::string(child->value(), child->value_size()));
            }
            else if (compare(child->local_name(), child->local_name_size(),
                             "DayOfMonth", strlen("DayOfMonth")))
            {
                day_of_month = std::stoul(
                    std::string(child->value(), child->value_size()));
            }
        }

#ifdef EWS_HAS_MAKE_UNIQUE
        return std::make_unique<absolute_yearly_recurrence>(
            std::move(day_of_month), std::move(mon));
#else
        return std::unique_ptr<absolute_yearly_recurrence>(
            new absolute_yearly_recurrence(std::move(day_of_month),
                                           std::move(mon)));
#endif
    }

    node = elem.first_node_ns(uri<>::microsoft::types(),
                              "RelativeYearlyRecurrence");
    if (node)
    {
        auto mon = month::jan;
        auto index = day_of_week_index::first;
        auto days_of_week = day_of_week::sun;

        for (auto child = node->first_node(); child;
             child = child->next_sibling())
        {
            if (compare(child->local_name(), child->local_name_size(), "Month",
                        strlen("Month")))
            {
                mon = str_to_month(
                    std::string(child->value(), child->value_size()));
            }
            else if (compare(child->local_name(), child->local_name_size(),
                             "DayOfWeekIndex", strlen("DayOfWeekIndex")))
            {
                index = str_to_day_of_week_index(
                    std::string(child->value(), child->value_size()));
            }
            else if (compare(child->local_name(), child->local_name_size(),
                             "DaysOfWeek", strlen("DaysOfWeek")))
            {
                days_of_week = str_to_day_of_week(
                    std::string(child->value(), child->value_size()));
            }
        }

#ifdef EWS_HAS_MAKE_UNIQUE
        return std::make_unique<relative_yearly_recurrence>(
            std::move(days_of_week), std::move(index), std::move(mon));
#else
        return std::unique_ptr<relative_yearly_recurrence>(
            new relative_yearly_recurrence(std::move(days_of_week),
                                           std::move(index), std::move(mon)));
#endif
    }

    node = elem.first_node_ns(uri<>::microsoft::types(),
                              "AbsoluteMonthlyRecurrence");
    if (node)
    {
        uint32_t interval = 0U;
        uint32_t day_of_month = 0U;

        for (auto child = node->first_node(); child;
             child = child->next_sibling())
        {
            if (compare(child->local_name(), child->local_name_size(),
                        "Interval", strlen("Interval")))
            {
                interval = std::stoul(
                    std::string(child->value(), child->value_size()));
            }
            else if (compare(child->local_name(), child->local_name_size(),
                             "DayOfMonth", strlen("DayOfMonth")))
            {
                day_of_month = std::stoul(
                    std::string(child->value(), child->value_size()));
            }
        }

#ifdef EWS_HAS_MAKE_UNIQUE
        return std::make_unique<absolute_monthly_recurrence>(
            std::move(interval), std::move(day_of_month));
#else
        return std::unique_ptr<absolute_monthly_recurrence>(
            new absolute_monthly_recurrence(std::move(interval),
                                            std::move(day_of_month)));
#endif
    }

    node = elem.first_node_ns(uri<>::microsoft::types(),
                              "RelativeMonthlyRecurrence");
    if (node)
    {
        uint32_t interval = 0U;
        auto days_of_week = day_of_week::sun;
        auto index = day_of_week_index::first;

        for (auto child = node->first_node(); child;
             child = child->next_sibling())
        {
            if (compare(child->local_name(), child->local_name_size(),
                        "Interval", strlen("Interval")))
            {
                interval = std::stoul(
                    std::string(child->value(), child->value_size()));
            }
            else if (compare(child->local_name(), child->local_name_size(),
                             "DaysOfWeek", strlen("DaysOfWeek")))
            {
                days_of_week = str_to_day_of_week(
                    std::string(child->value(), child->value_size()));
            }
            else if (compare(child->local_name(), child->local_name_size(),
                             "DayOfWeekIndex", strlen("DayOfWeekIndex")))
            {
                index = str_to_day_of_week_index(
                    std::string(child->value(), child->value_size()));
            }
        }

#ifdef EWS_HAS_MAKE_UNIQUE
        return std::make_unique<relative_monthly_recurrence>(
            std::move(interval), std::move(days_of_week), std::move(index));
#else
        return std::unique_ptr<relative_monthly_recurrence>(
            new relative_monthly_recurrence(std::move(interval),
                                            std::move(days_of_week),
                                            std::move(index)));
#endif
    }

    node = elem.first_node_ns(uri<>::microsoft::types(), "WeeklyRecurrence");
    if (node)
    {
        uint32_t interval = 0U;
        auto days_of_week = std::vector<day_of_week>();
        auto first_day_of_week = day_of_week::mon;

        for (auto child = node->first_node(); child;
             child = child->next_sibling())
        {
            if (compare(child->local_name(), child->local_name_size(),
                        "Interval", strlen("Interval")))
            {
                interval = std::stoul(
                    std::string(child->value(), child->value_size()));
            }
            else if (compare(child->local_name(), child->local_name_size(),
                             "DaysOfWeek", strlen("DaysOfWeek")))
            {
                const auto list =
                    std::string(child->value(), child->value_size());
                std::stringstream sstr(list);
                std::string temp;
                while (std::getline(sstr, temp, ' '))
                {
                    days_of_week.emplace_back(str_to_day_of_week(temp));
                }
            }
            else if (compare(child->local_name(), child->local_name_size(),
                             "FirstDayOfWeek", strlen("FirstDayOfWeek")))
            {
                first_day_of_week = str_to_day_of_week(
                    std::string(child->value(), child->value_size()));
            }
        }

#ifdef EWS_HAS_MAKE_UNIQUE
        return std::make_unique<weekly_recurrence>(
            std::move(interval), std::move(days_of_week),
            std::move(first_day_of_week));
#else
        return std::unique_ptr<weekly_recurrence>(
            new weekly_recurrence(std::move(interval), std::move(days_of_week),
                                  std::move(first_day_of_week)));
#endif
    }

    node = elem.first_node_ns(uri<>::microsoft::types(), "DailyRecurrence");
    if (node)
    {
        uint32_t interval = 0U;

        for (auto child = node->first_node(); child;
             child = child->next_sibling())
        {
            if (compare(child->local_name(), child->local_name_size(),
                        "Interval", strlen("Interval")))
            {
                interval = std::stoul(
                    std::string(child->value(), child->value_size()));
            }
        }

#ifdef EWS_HAS_MAKE_UNIQUE
        return std::make_unique<daily_recurrence>(std::move(interval));
#else
        return std::unique_ptr<daily_recurrence>(
            new daily_recurrence(std::move(interval)));
#endif
    }

    check(false, "Expected one of "
                 "<AbsoluteYearlyRecurrence>, <RelativeYearlyRecurrence>, "
                 "<AbsoluteMonthlyRecurrence>, <RelativeMonthlyRecurrence>, "
                 "<WeeklyRecurrence>, <DailyRecurrence>");
    return std::unique_ptr<recurrence_pattern>();
}

inline std::unique_ptr<recurrence_range>
recurrence_range::from_xml_element(const rapidxml::xml_node<>& elem)
{
    using rapidxml::internal::compare;

    check(compare(elem.local_name(), elem.local_name_size(), "Recurrence",
                  strlen("Recurrence")),
          "Expected a <Recurrence> element");

    auto node = elem.first_node_ns(internal::uri<>::microsoft::types(),
                                   "NoEndRecurrence");
    if (node)
    {
        date_time start_date;

        for (auto child = node->first_node(); child;
             child = child->next_sibling())
        {
            if (compare(child->local_name(), child->local_name_size(),
                        "StartDate", strlen("StartDate")))
            {
                start_date =
                    date_time(std::string(child->value(), child->value_size()));
            }
        }

#ifdef EWS_HAS_MAKE_UNIQUE
        return std::make_unique<no_end_recurrence_range>(std::move(start_date));
#else
        return std::unique_ptr<no_end_recurrence_range>(
            new no_end_recurrence_range(std::move(start_date)));
#endif
    }

    node = elem.first_node_ns(internal::uri<>::microsoft::types(),
                              "EndDateRecurrence");
    if (node)
    {
        date_time start_date;
        date_time end_date;

        for (auto child = node->first_node(); child;
             child = child->next_sibling())
        {
            if (compare(child->local_name(), child->local_name_size(),
                        "StartDate", strlen("StartDate")))
            {
                start_date =
                    date_time(std::string(child->value(), child->value_size()));
            }
            else if (compare(child->local_name(), child->local_name_size(),
                             "EndDate", strlen("EndDate")))
            {
                end_date =
                    date_time(std::string(child->value(), child->value_size()));
            }
        }

#ifdef EWS_HAS_MAKE_UNIQUE
        return std::make_unique<end_date_recurrence_range>(
            std::move(start_date), std::move(end_date));
#else
        return std::unique_ptr<end_date_recurrence_range>(
            new end_date_recurrence_range(std::move(start_date),
                                          std::move(end_date)));
#endif
    }

    node = elem.first_node_ns(internal::uri<>::microsoft::types(),
                              "NumberedRecurrence");
    if (node)
    {
        date_time start_date;
        uint32_t no_of_occurrences = 0U;

        for (auto child = node->first_node(); child;
             child = child->next_sibling())
        {
            if (compare(child->local_name(), child->local_name_size(),
                        "StartDate", strlen("StartDate")))
            {
                start_date =
                    date_time(std::string(child->value(), child->value_size()));
            }
            else if (compare(child->local_name(), child->local_name_size(),
                             "NumberOfOccurrences",
                             strlen("NumberOfOccurrences")))
            {
                no_of_occurrences = std::stoul(
                    std::string(child->value(), child->value_size()));
            }
        }

#ifdef EWS_HAS_MAKE_UNIQUE
        return std::make_unique<numbered_recurrence_range>(
            std::move(start_date), std::move(no_of_occurrences));
#else
        return std::unique_ptr<numbered_recurrence_range>(
            new numbered_recurrence_range(std::move(start_date),
                                          std::move(no_of_occurrences)));
#endif
    }

    check(false,
          "Expected one of "
          "<NoEndRecurrence>, <EndDateRecurrence>, <NumberedRecurrence>");
    return std::unique_ptr<recurrence_range>();
}
} // namespace ews
