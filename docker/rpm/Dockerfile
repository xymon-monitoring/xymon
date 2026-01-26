ARG OS
FROM $OS

RUN yum update -y && yum upgrade -y
RUN yum -y install gcc make pcre-devel

COPY . /xymon

COPY docker/rpm/start.sh start.sh
RUN chmod +x start.sh

ENTRYPOINT ["./start.sh"]
