//
// detail/fd_set_adapter.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2016 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_FD_SET_ADAPTER_HPP
#define ASIO_DETAIL_FD_SET_ADAPTER_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "./config.hpp"

#if !defined(ASIO_WINDOWS_RUNTIME)

#include "./posix_fd_set_adapter.hpp"
#include "./win_fd_set_adapter.hpp"

namespace asio {
namespace detail {

#if defined(ASIO_WINDOWS) || defined(__CYGWIN__)
typedef win_fd_set_adapter fd_set_adapter;
#else
typedef posix_fd_set_adapter fd_set_adapter;
#endif

} // namespace detail
} // namespace asio

#endif // !defined(ASIO_WINDOWS_RUNTIME)

#endif // ASIO_DETAIL_FD_SET_ADAPTER_HPP
