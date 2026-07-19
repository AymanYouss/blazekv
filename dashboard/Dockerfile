# syntax=docker/dockerfile:1

# ---- build the static bundle ----
FROM node:20-alpine AS build
WORKDIR /app
RUN corepack enable
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml ./
RUN pnpm install --frozen-lockfile
COPY . .
RUN pnpm build

# ---- serve via nginx, proxying /api and /metrics to BlazeKV ----
FROM nginx:1.27-alpine AS runtime
COPY --from=build /app/dist /usr/share/nginx/html
COPY nginx.conf.template /etc/nginx/templates/default.conf.template
# BLAZEKV_API is substituted into the nginx config at container start.
ENV BLAZEKV_API=http://blazekv:9121
EXPOSE 80
