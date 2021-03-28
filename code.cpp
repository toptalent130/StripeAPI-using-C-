 public async Task<StripeApiResponse> Charge([FromForm]PaymentChargeRequest model)
        {
            LogErrorRequest error;
            var response = new StripeApiResponse();
            try
            {
                var user = await _userManager.FindByIdAsync(UserId);
                var customerId = user.CustomerStripeId;
                var freeplan = await _stripePaymentService.GetGlobalPlanDetail((int)GlobalPlanEnum.Free);
                if (freeplan.AccessId == model.planId)
                {
                    if (!string.IsNullOrEmpty(user.CustomerStripeId))
                    {
                        CancelSubscription(user.CustomerStripeId);
                    }
                    response.StatusCode = HttpStatusCode.OK;
                    return response;
                }
                //Create Token
                //var options = new TokenCreateOptions
                //{
                //    Card = new CreditCardOptions
                //    {
                //        Number = "4242424242424242",
                //        ExpYear = 2020,
                //        ExpMonth = 11,
                //        Cvc = "123"
                //    }
                //};
                //Create Token END
                var customers = new CustomerService();
                var subscriptions = new SubscriptionService();
                if (user == null)
                {
                    //var customer = customers.Create(new CustomerCreateOptions
                    //{
                    //    Name = user.Company,
                    //    Email = user.Email,
                    //    Source = stripeToken.Id
                    //});
                    //var customerId = customer.Id;
                    //var plan = new PlanService().Get(model.planId);
                    //var charge = charges.Create(new ChargeCreateOptions
                    //{
                    //    Amount = plan.Amount,
                    //    Description = new ProductService().Get(plan.ProductId).Description,
                    //    Currency = "usd",
                    //    CustomerId = customerId
                    //});

                    //CreateCustomerSubscription(subscriptions, customerId, plan);

                    //user = await _userManager.FindByIdAsync(UserId);
                    //user.CustomerStripeId = customerId;
                    //await _userManager.UpdateAsync(user);

                    response.StatusCode = HttpStatusCode.OK;
                    return response;
                }
                else
                {
                    var service = new TokenService();
                    // Token stripeToken = service.Create(options);
                    if (string.IsNullOrEmpty(user.CustomerStripeId))
                    {
                        var customer = customers.Create(new CustomerCreateOptions
                        {
                            Name = user.Company,
                            Email = user.Email,
                            //Source = stripeToken.Id
                            Source = model.stripeToken
                        });
                        customerId = customer.Id;
                    }
                    var plan = new PlanService().Get(model.planId);
                    var rs = CreateCustomerSubscription(subscriptions, customerId, plan);
                    user.CustomerStripeId = customerId;

                    if (rs.Status == "active")
                    {
                        await _userManager.UpdateAsync(user);
                        response.StatusCode = HttpStatusCode.OK;
                        return response;
                    }
                    else
                    {
                        return response;
                    }



                }
            }
            catch (Exception ex)
            {
                response.StatusCode = HttpStatusCode.InternalServerError;
                response.ErrorMessage = ex.Message;

                error = new LogErrorRequest()
                {
                    ErrorMessage = ex.ToString(),
                    ErrorUrl = "/payment/charge"
                };
                _customLogService.LogError(error);
                return response;
            }
        }