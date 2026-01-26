ARG OS
FROM $OS

COPY . /xymon
RUN ls -l
RUN ls -l xymon

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install build-essential libpcre2-dev

COPY docker/debian/start.sh start.sh
RUN chmod +x start.sh

ENTRYPOINT ["./start.sh"]
