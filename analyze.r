#! /usr/bin/env Rscript

printf <- function(...) cat(sprintf(...))

import <- function(kind) {
	tmp <- c()
	for (f in list.files(pattern=paste(kind, ".*.txt", sep=""))) {
		data <- read.table(f, header=TRUE)
		size <- data$size[1]
		n <- length(data$us)
		min <- min(data$us)
		max <- max(data$us)
		mean <- mean(data$us)
		sd <- sd(data$us)
		codemean <- mean(data$codeus)
		codesd <- sd(data$codeus)
		median <- median(data$us)
		q25 <- quantile(data$us, probs = c(.25))
		q75 <- quantile(data$us, probs = c(.75))
	        # printf("%s %d %f\n", kind, size, mean)
		tmp$size <- append(tmp$size, size)
		tmp$n <- append(tmp$n, n)
		tmp$min <- append(tmp$min, min)
		tmp$max <- append(tmp$max, max)
		tmp$mean <- append(tmp$mean, mean)
		tmp$sd <- append(tmp$sd, sd)
		tmp$codemean <- append(tmp$codemean, codemean)
		tmp$codesd <- append(tmp$codesd, codesd)
		tmp$median <- append(tmp$median, median)
		tmp$q25 <- append(tmp$q25, q25)
		tmp$q75 <- append(tmp$q75, q75)
	}
	tmp <- data.frame(size=tmp$size, n=tmp$n, min=tmp$min, max=tmp$max,
	                  mean=tmp$mean, sd=tmp$sd, median=tmp$median,
	                  q25=tmp$q25, q75=tmp$q75, codemean=tmp$codemean,
	                  codesd=tmp$codesd)
	tmp <- tmp[order(tmp$size), ]
	return(tmp)
}

kern <- import("kern")
warp <- import("warp")

kern
warp

xmax <- max(kern$size, warp$size)
ymax <- max(kern$mean + kern$sd/2, warp$mean + warp$sd/2, kern$q75, warp$q75)#, kern$max, warp$max)
xr <- c(0, xmax)
yr <- c(0, ymax)

plot(0, type="n", xlim=xr, ylim=yr,
     xlab="UDP payload size [bytes]", ylab=expression(paste("RTT [", mu, "s]")),
     main=sprintf("Mean RTTs over %d iterations", kern$n[1]))
grid()
par(new=T)
plot(kern$size, kern$mean, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="blue")
# par(new=T)
# plot(kern$size, kern$min, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="l", lty=2, col="blue")
# par(new=T)
# plot(kern$size, kern$max, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="l", lty=2, col="blue")
segments(kern$size, kern$mean-kern$sd/2, kern$size, kern$mean+kern$sd/2, col="blue")
par(new=T)
plot(warp$size, warp$mean, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="red")
# par(new=T)
# plot(warp$size, warp$min, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="l", lty=2, col="red")
# par(new=T)
# plot(warp$size, warp$max, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="l", lty=2, col="red")
segments(warp$size, warp$mean-warp$sd/2, warp$size, warp$mean+warp$sd/2, col="red")
legend("topleft", c("kern", "warp"), col=c("blue", "red"), lty=1)

plot(0, type="n", xlim=xr, ylim=yr,
     xlab="UDP payload size [bytes]", ylab=expression(paste("RTT [", mu, "s]")),
     main=sprintf("Median RTTs over %d iterations", kern$n[1]))
grid()
par(new=T)
plot(kern$size, kern$median, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="blue")
segments(kern$size, kern$q25, kern$size, kern$q75, col="blue")
par(new=T)
plot(warp$size, warp$median, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="red")
segments(warp$size, warp$q25, warp$size, warp$q75, col="red")
legend("topleft", c("kern", "warp"), col=c("blue", "red"), lty=1)

