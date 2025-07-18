#pragma once
#include <coroutine>
#include <expected>
#include <liburing.h>
#include <optional>
#include <tuple>
#include <errno.h>
#include <type_traits>
#include "meta.h"
#include "coro_io_ctx.h"


namespace coro_io::awaiter {
    template<typename derived>
    struct base {
        io_uring_cqe cqe;
        bool await_ready() { return false; }
        void await_suspend(std::coroutine_handle<void> handle) {
            coro_io_ctx::get_instance().submit(
                    handle,
                    &cqe,
                    false, 
                    nullptr, 
                    this, 
                    [](void* helper_ptr, io_uring_sqe* sqe) {
                        static_cast<derived*>(helper_ptr)->setup(sqe);
                    }
            );
        }
        io_uring_cqe await_resume() { return cqe; }

        void setup(io_uring_sqe* sqe) { std::terminate();} // Default setup, can be overridden by derived classes
    };


    struct read : base<read> {
        int fd;
        void* buf;
        size_t len;
        off_t offset;

        read(int fd, void* buf, size_t len, off_t offset = 0)
            : fd(fd), buf(buf), len(len), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_read(sqe, fd, buf, len, offset);
        }
    };


    struct write : base<write> {
        int fd;
        const void* buf;
        unsigned int len;
        off_t offset;

        write(int fd, const void* buf, unsigned int len, off_t offset = 0)
            : fd(fd), buf(buf), len(len), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_write(sqe, fd, buf, len, offset);
        }
    };


    struct readv : base<readv> {
        int fd;
        const iovec* iov;
        unsigned nr_iov;
        off_t offset;

        readv(int fd, const iovec* iov, unsigned nr_iov = 1, off_t offset = 0)
            : fd(fd), iov(iov), nr_iov(nr_iov), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_readv(sqe, fd, iov, nr_iov, offset);
        }
    };



    struct writev : base<writev> {
        int fd;
        const iovec* iov;
        unsigned nr_iov;
        off_t offset;
        writev() = default;
        writev(int fd, const iovec* iov, unsigned nr_iov, off_t offset = 0)
            : fd(fd), iov(iov), nr_iov(nr_iov), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_writev(sqe, fd, iov, nr_iov, offset);
        }
    };



    struct accept : base<accept> {
        int fd;
        sockaddr* addr;
        socklen_t* addrlen;
        int flags;
        accept(int fd, sockaddr* addr, socklen_t* addrlen, int flags = 0)
            : fd(fd), addr(addr), addrlen(addrlen), flags(flags) {}
        void setup(io_uring_sqe* sqe) {
            io_uring_prep_accept(sqe, fd, addr, addrlen, flags);
        }
    };





    template<typename io_awaiter_t>
        requires std::is_base_of_v<base<io_awaiter_t>, io_awaiter_t>
    struct link_timeout {
        __kernel_timespec ts;
        io_awaiter_t awaiter;
        bool await_ready() { return false; }
        void await_suspend(std::coroutine_handle<void> handle) {
            coro_io_ctx::get_instance().submit(
                    handle,
                    &awaiter.cqe,
                    true, 
                    &ts, 
                    &awaiter, 
                    [](void* helper_ptr, io_uring_sqe* sqe) {
                        static_cast<io_awaiter_t*>(helper_ptr)->setup(sqe);
                    }
                
            );
        }
        std::optional<io_uring_cqe> await_resume() { 
            if (awaiter.cqe.res == -ECANCELED) {
                return std::nullopt;
            }
            return awaiter.cqe;
        }
        link_timeout() = default;


        template<typename duration_t>
            requires seele::meta::is_specialization_of_v<std::decay_t<duration_t>, std::chrono::duration>
        link_timeout(io_awaiter_t&& awaiter, duration_t&& duration) : 
            ts(std::chrono::duration_cast<std::chrono::seconds>(duration).count(),
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                duration - std::chrono::seconds(ts.tv_sec)).count()),
            awaiter(std::forward<io_awaiter_t>(awaiter)){}

    };

    template<typename io_awaiter_t, typename duration_t>
    link_timeout(io_awaiter_t&&, duration_t&&) -> link_timeout<std::decay_t<io_awaiter_t>>;


    template<typename function_t, typename... args_t>
        requires std::is_invocable_v<function_t, io_uring_sqe*, args_t...>
    struct any : base<any<function_t, args_t...>> {
        std::tuple<args_t...> args;
        function_t func;
        any(function_t&& func, args_t&&... args)
            : args(std::forward<args_t>(args)...), func(std::forward<function_t>(func)) {}

        void setup(io_uring_sqe* sqe) {
            // This is a placeholder, actual implementation would depend on the function type
            // and how it interacts with io_uring.
        }
    };
}
